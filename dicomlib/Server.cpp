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



#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>

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
	namespace
	{
		bool IsStrictImplementationUID(const std::string& uid)
		{
			if(uid.empty() || uid.size()>64)
				return false;
			bool previousWasDot = false;
			for(size_t i=0;i<uid.size();++i)
			{
				const unsigned char c = static_cast<unsigned char>(uid[i]);
				if(uid[i]=='.')
				{
					if(i==0 || previousWasDot)
						return false;
					previousWasDot = true;
				}
				else if(std::isdigit(c))
					previousWasDot = false;
				else
					return false;
			}
			return !previousWasDot;
		}
	}

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
    void Server::SetForkPerAssociation(bool enabled)
    {
        std::lock_guard<std::mutex> scoped_lock(mutex_);
        forkPerAssociation_=enabled;
    }

    /*!
        Récolte les fils terminés, sans bloquer.

        Sans waitpid(), chaque association laisserait un zombie — une entrée dans
        la table des processus jamais rendue. Après quelques milliers
        d'impressions, plus aucun fork ne serait possible.
    */
    void Server::reapFinishedChildren()
    {
        int status=0;
        while(::waitpid(-1,&status,WNOHANG)>0)
        {
            std::lock_guard<std::mutex> scoped_lock(mutex_);
            if(liveChildren_>0)
                --liveChildren_;
        }
    }

    /*!
        Bloque tant que le nombre de fils vivants atteint la limite.
        Voir Server.hpp pour la raison d'être — c'est le père qui compte.
    */
    void Server::waitForChildSlot()
    {
        for(;;)
        {
            reapFinishedChildren();

            size_t live=0,maximum=0;
            {
                std::lock_guard<std::mutex> scoped_lock(mutex_);
                live=liveChildren_;
                maximum=maxConcurrentAssociations_;
            }
            if(maximum==0 || live<maximum)
                return;

            int status=0;
            const pid_t pid=::waitpid(-1,&status,0);//bloquant : on attend une place
            if(pid>0)
            {
                std::lock_guard<std::mutex> scoped_lock(mutex_);
                if(liveChildren_>0)
                    --liveChildren_;
                continue;
            }
            if(errno==EINTR)
                continue;
            //Plus aucun fils à attendre alors que le compteur en annonce : il est
            //désynchronisé. Le remettre à zéro plutôt que boucler indéfiniment.
            {
                std::lock_guard<std::mutex> scoped_lock(mutex_);
                liveChildren_=0;
            }
            return;
        }
    }

    void Server::SetMaxConcurrentAssociations(size_t maximum)
    {
        std::lock_guard<std::mutex> scoped_lock(mutex_);
        maxConcurrentAssociations_=maximum;
    }

    /*!
        Prend un jeton d'association si la limite le permet.

        Comptabilisé APRÈS acceptation de l'A-ASSOCIATE-RQ mais AVANT tout
        traitement : c'est le moment où l'association va commencer à consommer
        de la mémoire.
    */
    bool Server::TryBeginAssociation()
    {
        std::lock_guard<std::mutex> scoped_lock(mutex_);
        if(maxConcurrentAssociations_!=0 &&
           activeAssociations_>=maxConcurrentAssociations_)
            return false;
        ++activeAssociations_;
        return true;
    }

    void Server::EndAssociation()
    {
        std::lock_guard<std::mutex> scoped_lock(mutex_);
        if(activeAssociations_>0)
            --activeAssociations_;
    }

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

            {
                bool forkMode=false;
                {
                    std::lock_guard<std::mutex> scoped_lock(mutex_);
                    forkMode=forkPerAssociation_;
                }
                //Avant l'accept : la connexion de trop reste dans le backlog du
                //noyau plutôt que d'être acceptée puis laissée en attente.
                if(forkMode)
                    waitForChildSlot();
            }

			std::unique_ptr<Network::AcceptedSocket> pAccepter =
				std::make_unique<Network::AcceptedSocket>(TheServerSocket);//blocks, waiting for a client.

			bool useFork=false;
			{
				std::lock_guard<std::mutex> scoped_lock(mutex_);
				useFork=forkPerAssociation_;
			}

			if(useFork)
			{
				/*
					Un processus par association (cf. SetForkPerAssociation).

					Le fils traite l'association puis sort par _exit() : le noyau
					reprend alors TOUTE sa mémoire, ce qu'un thread ne permet pas.
					_exit() et non exit() : les destructeurs globaux et le vidage
					des flux du père ne doivent pas être rejoués dans le fils.
				*/
				reapFinishedChildren();

				const pid_t pid=fork();
				if(pid==0)
				{
					//Le fils ne doit pas conserver la socket d'ECOUTE du père :
					//il l'empêcherait d'être libérée et pourrait accepter des
					//connexions à sa place.
					::close(TheServerSocket.GetSocketDescriptor());
					Implementation::ThreadSpecificServer threadServer(pAccepter.release(),*this);
					threadServer();
					_exit(0);
				}
				if(pid>0)
				{
					{
						std::lock_guard<std::mutex> scoped_lock(mutex_);
						++liveChildren_;
					}
					//Le père referme SA copie de la socket acceptée : sans cela
					//la connexion ne se fermerait jamais vraiment et chaque
					//descripteur fuirait. (unique_ptr s'en charge en sortant.)
					continue;
				}
				//fork() a échoué : traiter dans le processus courant plutôt que
				//de perdre l'association.
				LogError("fork() impossible, association traitee dans le processus courant");
				Implementation::ThreadSpecificServer threadServer(pAccepter.release(),*this);
				threadServer();
				continue;
			}

			std::shared_ptr<std::thread> pThread =
				std::make_shared<std::thread>(
					theThreadFunction,
					pAccepter.get(),
					std::ref(*this));
			pAccepter.release();

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
			ServerThread_ = std::make_unique<std::thread>(s);
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
	void Server::SetSOPClassExtendedNegotiationCallback(SOPClassExtendedNegotiationFunction f)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		SOPClassExtendedNegotiationCallback_=f;
	}



	void Server::AssociationNegotiated(const primitive::AAssociateRQ& request)
	{
		return CurrentLogger_->AssociationNegotiated(request);

	}
	void Server::AssociationTerminated()
	{
		CurrentLogger_->AssociationTerminated();
	}
	void Server::OperationHandled(ServiceBase& service,UINT16 command)
	{
		CurrentLogger_->OperationHandled(service,command);
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
	bool Server::NegotiateSOPClassExtended(
		const UID& uid,
		const std::vector<BYTE>& request,
		std::vector<BYTE>& response)
	{
		std::lock_guard<std::mutex> scoped_lock(AETMutex_);
		return !SOPClassExtendedNegotiationCallback_?
			false
			:
			SOPClassExtendedNegotiationCallback_(uid,request,response);
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
		void Server::AddMoveStoreHandler(const UID& uid,CMoveStoreFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			MoveStoreHandlers_[uid]=Handler;
		}

		void Server::AddNEventReportHandler(const UID& uid,NHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NEventReportHandlers_[uid]=Handler;
		}

		void Server::AddNGetHandler(const UID& uid,NHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NGetHandlers_[uid]=
				[Handler](ServiceBase& service, const DataSet& command, const DataSet& requestData,
					DataSet& responseData, std::vector<Tag>&)
				{
					return Handler(service,command,requestData,responseData);
				};
		}

		void Server::AddNGetHandler(const UID& uid,NAttributeHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NGetHandlers_[uid]=Handler;
		}

		void Server::AddNSetHandler(const UID& uid,NHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NSetHandlers_[uid]=
				[Handler](ServiceBase& service, const DataSet& command, const DataSet& requestData,
					DataSet& responseData, std::vector<Tag>&)
				{
					return Handler(service,command,requestData,responseData);
				};
		}

		void Server::AddNSetHandler(const UID& uid,NAttributeHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NSetHandlers_[uid]=Handler;
		}

		void Server::AddNActionHandler(const UID& uid,NHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NActionHandlers_[uid]=Handler;
		}

		void Server::AddNCreateHandler(const UID& uid,NHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NCreateHandlers_[uid]=
				[Handler](ServiceBase& service, const DataSet& command, const DataSet& requestData,
					UID&, DataSet& responseData, std::vector<Tag>&)
				{
					return Handler(service,command,requestData,responseData);
				};
		}

		void Server::AddNCreateHandler(const UID& uid,NCreateHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NCreateHandlers_[uid]=
				[Handler](ServiceBase& service, const DataSet& command, const DataSet& requestData,
					UID& responseInstUID, DataSet& responseData, std::vector<Tag>&)
				{
					return Handler(service,command,requestData,responseInstUID,responseData);
				};
		}

		void Server::AddNCreateHandler(const UID& uid,NCreateAttributeHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NCreateHandlers_[uid]=Handler;
		}

		void Server::AddNDeleteHandler(const UID& uid,NHandlerFunction Handler)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			NDeleteHandlers_[uid]=Handler;
		}

		void Server::AddAcceptableAbstractSyntax(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			AcceptableAbstractSyntaxes_.insert(uid);
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

	bool Server::HasMoveStoreHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		return MoveStoreHandlers_.find(uid)!=MoveStoreHandlers_.end();
	}

	CMoveStoreFunction Server::GetMoveStoreHandler(const UID& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		std::map<UID,CMoveStoreFunction>::iterator I = MoveStoreHandlers_.find(uid);
		if(I==MoveStoreHandlers_.end())
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

		namespace
		{
			template<typename Handler>
			Handler GetNHandler(
				std::map<UID,Handler>& handlers,
				const UID& uid,
				Server& server)
			{
				typename std::map<UID,Handler>::iterator I = handlers.find(uid);
				if(I==handlers.end())
				{
					server.LogError("No available handler.");
					throw NoAvailableHandler();
				}
				return I->second;
			}
		}

		NHandlerFunction Server::GetNEventReportHandler(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			return GetNHandler(NEventReportHandlers_,uid,*this);
		}

		NAttributeHandlerFunction Server::GetNGetHandler(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			return GetNHandler(NGetHandlers_,uid,*this);
		}

		NAttributeHandlerFunction Server::GetNSetHandler(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			return GetNHandler(NSetHandlers_,uid,*this);
		}

		NHandlerFunction Server::GetNActionHandler(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			return GetNHandler(NActionHandlers_,uid,*this);
		}

		NCreateAttributeHandlerFunction Server::GetNCreateHandler(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			return GetNHandler(NCreateHandlers_,uid,*this);
		}

		NHandlerFunction Server::GetNDeleteHandler(const UID& uid)
		{
			std::lock_guard<std::mutex> scoped_lock(mutex_);
			return GetNHandler(NDeleteHandlers_,uid,*this);
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
		if(AcceptableAbstractSyntaxes_.find(uid)!=AcceptableAbstractSyntaxes_.end())
			return true;//explicitly accepted, e.g. a Meta SOP Class
		if((Handlers_.find(uid)!=Handlers_.end()) ||
			(FindHandlers_.find(uid)!=FindHandlers_.end()) ||
			(CancellableFindHandlers_.find(uid)!=CancellableFindHandlers_.end()) ||
				(CancellableGetHandlers_.find(uid)!=CancellableGetHandlers_.end()) ||
				(CancellableMoveHandlers_.find(uid)!=CancellableMoveHandlers_.end()) ||
				(MoveStoreHandlers_.find(uid)!=MoveStoreHandlers_.end()) ||
				(NEventReportHandlers_.find(uid)!=NEventReportHandlers_.end()) ||
				(NGetHandlers_.find(uid)!=NGetHandlers_.end()) ||
				(NSetHandlers_.find(uid)!=NSetHandlers_.end()) ||
				(NActionHandlers_.find(uid)!=NActionHandlers_.end()) ||
				(NCreateHandlers_.find(uid)!=NCreateHandlers_.end()) ||
				(NDeleteHandlers_.find(uid)!=NDeleteHandlers_.end()))
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
		ImpClass.UID_=ImplementationClassUIDOverride_.empty()?
			ImplementationClassUID
			:
			ImplementationClassUIDOverride_;
	}

	void Server::GetImplementationVersion(ImplementationVersion &ImpVersion)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);//not sure we really need this.
		ImpVersion.Name=ImplementationVersionNameOverride_.empty()?
			ImplementationVersionName
			:
			ImplementationVersionNameOverride_;
	}

	void Server::SetImplementationClassUID(const std::string& uid)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		if(!uid.empty() && !IsStrictImplementationUID(uid))
			return;
		ImplementationClassUIDOverride_=uid;
	}

	void Server::SetImplementationVersionName(const std::string& name)
	{
		std::lock_guard<std::mutex> scoped_lock(mutex_);
		//PS3.7 D.3.3.2.3, Table D.3-3: 1 to 16 characters. Refuse anything longer
		//rather than announce a non-conformant value.
		if(name.size()>16)
			return;
		ImplementationVersionNameOverride_=name;
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

			/*
				select() a expiré : c'est le SEUL moment où le père a la main
				pendant qu'il attend une connexion. On en profite pour récolter
				les fils terminés.

				Sans cela ils restent ZOMBIES jusqu'à la connexion suivante — leur
				mémoire est bien rendue au noyau, mais leur entrée dans la table
				des processus subsiste (visible en RES=0 dans htop). Après le
				dernier travail d'une série, plus rien ne déclenchait la récolte :
				les zombies s'accumulaient jusqu'à épuiser la table.
			*/
			{
				bool forkMode=false;
				{
					std::lock_guard<std::mutex> scoped_lock(mutex_);
					forkMode=forkPerAssociation_;
				}
				if(forkMode)
					reapFinishedChildren();
			}
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
