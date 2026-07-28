/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/

/*
	Design note.

	We make frequent use of callbacks here, implemented using std::function.

	This is necessary in a multithreaded, event-driven environment.  Basically we're saying
	'when event X occurs, call function Y'.  Y could be a callback or an overriden virtual function.

	In most cases we could use callbacks or virtual functions, and it's a question of choosing
	which is simpler.   Callbacks make sense for the dicom command handlers, as it means
	we can dynamically change the command handlers available by modifying a map of
	callbacks; the basic structure is this:
			std::map<UID,HandlerFunction> Handlers_;

*/



#include <exception>
#include <iostream>

#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Server.hpp"
#include "UIDs.hpp"
#include "ImplementationUID.hpp"
#include "pdata.hpp"
#include "TransferSyntax.hpp"
#include "Decoder.hpp"
#include "Cdimse.hpp"
#include "UID.hpp"
#include "ServiceBase.hpp"

#include "ThreadSpecificServer.hpp"

using std::string;using std::cout; using std::endl;

namespace dicom
{

	using namespace primitive;
	std::mutex Server::cerr_mutex;
	std::mutex Server::cout_mutex;

	MoveDestinationEndpoint::MoveDestinationEndpoint()
		:host()
		,port(0)
	{
	}

	MoveDestinationEndpoint::MoveDestinationEndpoint(
		const std::string& destinationHost,
		unsigned short destinationPort)
		:host(destinationHost)
		,port(destinationPort)
	{
	}

	void Server::Logger::LogError(std::string Error)
	{
 		std::lock_guard<std::mutex> lock(cerr_mutex);
 		std::cerr << Error << std::endl;
	}

	void Server::Logger::LogMessage	(std::string Message)
	{
 		std::lock_guard<std::mutex> lock(cout_mutex);
 		std::cout << Message << std::endl;
	}

	Server::Server()
		:ServerThread_()
		,KillFlag(false)
		,CurrentLogger_(&DefaultLogger_)
	{
		//is the following line a standard requirment???
		//AcceptableAbstractSyntaxes_.insert(UID_VERIFICATION_SOP_CLASS);
	}

	Server::~Server()
	{
		this->Stop();

	}


    namespace
    {
        /*
            If this works nicely, the next step would be to maybe ACCEPT the socket in THIS thread
            and destroy it at the end of the function.
        */

        void theThreadFunction(Network::AcceptedSocket* socket,Server& server)
        {
            Implementation::ThreadSpecificServer threadServer(socket,server);
            threadServer();
            server.allDone();

        }
    }

    /*!
        Indicate that the currently executing thread can be safely deleted.
        We do this by comparing thread identifiers in a cross-platform way via thread::operator == 
    */
    void Server::allDone()
    {
        std::lock_guard<std::mutex> scoped_lock(mutex_);

		std::thread::id currentThreadId=std::this_thread::get_id();
        for(ThreadGroup::iterator i=clientThreads_.begin();i!=clientThreads_.end();i++)
        {
            if((i->first->get_id())==currentThreadId)
            {
                i->second=true;//ie, ready for cleanup
                return;
            }
        }
        //if we get this far, something horrible has happened!
        throw std::runtime_error("Current thread not managed by clientThreads_!");
    }

    /*!
        If we specify cleanAll, then we'll wait for each thread to finish and clean it up.
        Otherwise we'll just clean up threads that have reported themselves to be finished.
    */
    void Server::threadCleanup(bool cleanAll)
    {
        std::lock_guard<std::mutex> scoped_lock(mutex_);
        for(ThreadGroup::iterator i=clientThreads_.begin();i!=clientThreads_.end();)
        {
            bool threadDone=i->second;
            if(threadDone || cleanAll)
            {
                i->first->join();
                clientThreads_.erase(i++); //only iterator being erased gets invalidated, so this is ok.
            }
            else
                ++i;
        }
    }

	//!Start listening for connections on 'port'
	/*!
		Note that this runs in the current thread, and will not exit until the kill
		flag is raised. If you want the server to run in the background, use
		ServeInNewThread()
	*/
    void Server::Serve(short port)
    {

        //PRECONDITIONS:Server is not running.

		//here's where we ask the framework to start listening
		//on the relevant port, and start up server threads
		//as needed.

        if(!clientThreads_.empty())
            throw std::runtime_error("client threads currently running!");

		//Open a socket to listen for new connections.
		Network::ServerSocket TheServerSocket(port);

		while(ClientConnectionPending(&TheServerSocket))
		{
            threadCleanup(false);

			Network::AcceptedSocket* pAccepter=new Network::AcceptedSocket(TheServerSocket);//blocks, waiting for a client.

            std::shared_ptr<std::thread> pThread(new std::thread(theThreadFunction,pAccepter,std::ref(*this)));

            {     
                std::lock_guard<std::mutex> scoped_lock(mutex_);
                clientThreads_.insert(ThreadGroup::value_type(pThread,false));
            }
			//Note that the socket object must be deleted in the newly created thread.
			//see comments in ThreadSpecificServer::operator()

		}
		//if we get here, the kill flag has been raised, so wait for
		//all threads to terminate nicely...
        
        threadCleanup(true);
	}


	//!Functor that gets passed to new thread.
	/*!
		Gets called by newly created server thread, and runs
		server_ in new thread.
	*/
	struct ServerThreadStarter
	{
		Server& server_;
		short port_;
		ServerThreadStarter(Server& server,short port):server_(server),port_(port){}
		void operator()()
		{
			server_.Serve(port_);
		}
	};


	//!Starts Server running in new thread.
	/*!
		This is useful if your application is more than just a DICOM
		server (for example, a GUI app that can receive DICOM C-STORE
		requests) as it enables you to run a server on a different thread
		than the main event loop.
	*/
	void Server::ServeInNewThread(short port)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		if(ServerThread_)
			return;//we're already doing it....

		KillFlag=false;//Sam add this so that server can be restarted. 8April2009
		ServerThreadStarter s(*this,port);
		ServerThread_.reset(new std::thread(s));
	}


	//!This is kind of pointless as there is only one application context acceptable in the standard.
	bool Server::IsAcceptableApplicationContext(const UID& uid)
	{
		return (uid==APPLICATION_CONTEXT);
	}

	/*
	Callback setting functions.
	*/

	void Server::SetCheckLocalAETCallback(StringCheckFunction f)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		CheckLocalAET=f;
	}
	void Server::SetCheckRemoteAETCallback(StringCheckFunction2 f)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		CheckRemoteAET=f;
	}
	void Server::SetMoveDestinationResolverCallback(MoveDestinationResolverFunction f)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		ResolveMoveDestinationCallback_=f;
	}



	void Server::AssociationNegotiated(const primitive::AAssociateRQ& request)
	{
		return CurrentLogger_->AssociationNegotiated(request);

	}
	void Server::AssociationTerminated()
	{
		CurrentLogger_->AssociationTerminated();
	}

	bool Server::IsAcceptableRemoteApplicationTitle(const std::string& title,std::string ip)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		return !CheckRemoteAET?
			false
			:
			CheckRemoteAET(title,ip);
	}
	bool Server::IsAcceptableLocalApplicationTitle(const std::string& title)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		return !CheckLocalAET?
			false
			:
			CheckLocalAET(title);
	}
	bool Server::ResolveMoveDestination(const std::string& title,MoveDestinationEndpoint& endpoint)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		return !ResolveMoveDestinationCallback_?
			false
			:
			ResolveMoveDestinationCallback_(title,endpoint);
	}
	void Server::AddHandler(const UID& uid,HandlerFunction Handler)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		Handlers_[uid]=Handler;
	}
	void Server::AddFindHandler(const UID& uid,CFindFunction Handler)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		FindHandlers_[uid]=Handler;
	}
	void Server::AddCancellableFindHandler(const UID& uid,CFindStatusFunction Handler)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		CancellableFindHandlers_[uid]=Handler;
	}
	void Server::AddCancellableGetHandler(const UID& uid,CGetStatusFunction Handler)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		CancellableGetHandlers_[uid]=Handler;
	}
	void Server::AddCancellableMoveHandler(const UID& uid,CMoveStatusFunction Handler)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		CancellableMoveHandlers_[uid]=Handler;
	}

	bool Server::HasCancellableFindHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		return CancellableFindHandlers_.find(uid)!=CancellableFindHandlers_.end();
	}

	CFindStatusFunction Server::GetCancellableFindHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		std::map<UID,CFindStatusFunction>::iterator I = CancellableFindHandlers_.find(uid);
		if(I==CancellableFindHandlers_.end())
		{
			LogError("No available handler.");
			throw NoAvailableHandler();//or something
		}
		else
			return I->second;
	}

	bool Server::HasCancellableGetHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		return CancellableGetHandlers_.find(uid)!=CancellableGetHandlers_.end();
	}

	CGetStatusFunction Server::GetCancellableGetHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		std::map<UID,CGetStatusFunction>::iterator I = CancellableGetHandlers_.find(uid);
		if(I==CancellableGetHandlers_.end())
		{
			LogError("No available handler.");
			throw NoAvailableHandler();//or something
		}
		else
			return I->second;
	}

	bool Server::HasCancellableMoveHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		return CancellableMoveHandlers_.find(uid)!=CancellableMoveHandlers_.end();
	}

	CMoveStatusFunction Server::GetCancellableMoveHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		std::map<UID,CMoveStatusFunction>::iterator I = CancellableMoveHandlers_.find(uid);
		if(I==CancellableMoveHandlers_.end())
		{
			LogError("No available handler.");
			throw NoAvailableHandler();//or something
		}
		else
			return I->second;
	}

	CFindFunction Server::GetFindHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		std::map<UID,CFindFunction>::iterator I = FindHandlers_.find(uid);
		if(I==FindHandlers_.end())
		{
			LogError("No available handler.");
			throw NoAvailableHandler();//or something
		}
		else
			return I->second;
	}
	HandlerFunction Server::GetHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		std::map<UID,HandlerFunction>::iterator I = Handlers_.find(uid);
		if(I==Handlers_.end())
		{
			LogError("No available handler.");
			throw NoAvailableHandler();
		}
		else
			return I->second;
	}


	//!i.e, "Do we have a function capable of handling a command of type uid"
	bool Server::IsAcceptableAbstractSyntax(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		if((Handlers_.find(uid)!=Handlers_.end()) ||
			(FindHandlers_.find(uid)!=FindHandlers_.end()) ||
			(CancellableFindHandlers_.find(uid)!=CancellableFindHandlers_.end()) ||
			(CancellableGetHandlers_.find(uid)!=CancellableGetHandlers_.end()) ||
			(CancellableMoveHandlers_.find(uid)!=CancellableMoveHandlers_.end()))
			return true;
		if(VERIFICATION_SOP_CLASS==uid)
			return true;//we accept this by default.
		return false;
		//return (Handlers_.find(uid)!=Handlers_.end());
	}

	bool Server::CanHandleTransferSyntax(TransferSyntax &TrnSyntax)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		if(!IsTransferSyntaxUID(TrnSyntax.UID_))
			return false;
		TS ts(TrnSyntax.UID_);
		return ts.canDecodeDataset() || ts.canPassThroughPixelData() || ts.hasCompiledPixelCodec();
	}


	void Server::GetImplementationClass(ImplementationClass &ImpClass)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);//do we really need this?
		ImpClass.UID_=ImplementationClassUID;
	}

	void Server::GetImplementationVersion(ImplementationVersion &ImpVersion)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);//not sure we really need this.
		ImpVersion.Name=ImplementationVersionName;
	}



	/*!
		Waits for a connection to be made, checking every few seconds
		for KillFlagRaised.
	*/
	bool Server::ClientConnectionPending(Network::Socket* pSocket)
	{
		for(;;)
		{
			fd_set rfds;//need to reset all this stuff after each call to select.
			timeval tv;

			FD_ZERO(&rfds);
			FD_SET(pSocket->GetSocketDescriptor(), &rfds);
			/* Wait briefly so Stop() can terminate a background server deterministically. */
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			if(KillFlagRaised())
			{
				LogMessage("Kill flag raised...");
				break;
			}
			//select() examines the state of the socket.
			int retval = select((int)pSocket->GetSocketDescriptor()+1, &rfds, NULL, NULL, &tv);

			if(KillFlagRaised())
				break;
			if(retval==-1)
				throw SystemError("Select");
			if (retval)
				return true;
		}
		LogMessage ("Kill flag raised, accepting no more connections.");
		return false;
	}



	/*!
		presumably should call RaiseKillFlag and wait until all
		threads are stopped?

		Should it raise an error if it's not running?
	*/
	void Server::Stop()
	{
		RaiseKillFlag();

		std::unique_ptr<std::thread> serverThread;
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			serverThread = std::move(ServerThread_);
		}

		if(serverThread && serverThread->joinable())
		{
			if(serverThread->get_id() == std::this_thread::get_id())
				serverThread->detach();
			else
				serverThread->join();
		}
	}



	bool Server::KillFlagRaised()
	{

		std::lock_guard<std::mutex> lock(killflag_mutex);
		return KillFlag;
	}
	void Server::RaiseKillFlag()
	{
		std::lock_guard<std::mutex> lock(killflag_mutex);
		KillFlag=true;
	}


	void Server::LogError(std::string Error)
	{
		CurrentLogger_->LogError(Error);
	}

	void Server::LogMessage(std::string Message)
	{
		CurrentLogger_->LogMessage(Message);
	}

	void Server::SetLogger(Logger* logger)
	{
		CurrentLogger_=logger;
	}

}//namespace dicom




