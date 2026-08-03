#ifndef SERVER_HPP_INCLUDE_GUARD_26510884
#define SERVER_HPP_INCLUDE_GUARD_26510884

#if defined( __unix__)
#include <signal.h>
#endif

#include <functional>
#include <memory>
#include <exception>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "socket/Socket.hpp"
#include "Cdimse.hpp"
#include "Ndimse.hpp"
#include "ServiceBase.hpp"
#include "DataSet.hpp"
#include "aarq.hpp"
namespace dicom
{
	namespace Implementation
	{
		struct ThreadSpecificServer;
	}

	struct TerminateServerThread : public std::exception{};
/*
	The alternative to setting up all these callbacks would be to use
	inheritance and virtual functions.
*/

	typedef std::function<bool(const std::string&)> StringCheckFunction;
	typedef std::function<bool(const std::string&,const std::string)> StringCheckFunction2;
	typedef std::function<void()> AssociationTerminatedFunction;
	typedef std::function<void(const primitive::AAssociateRQ&)> AssociationNegotiatedFunction;

	struct MoveDestinationEndpoint
	{
		std::string host;
		unsigned short port;

		MoveDestinationEndpoint();
		MoveDestinationEndpoint(const std::string& destinationHost, unsigned short destinationPort);
	};

	typedef std::function<bool(const std::string&,MoveDestinationEndpoint&)>
		MoveDestinationResolverFunction;
	typedef std::function<bool(const UID&,const std::vector<BYTE>&,std::vector<BYTE>&)>
		SOPClassExtendedNegotiationFunction;


	//!Thrown if we don't have a handler function for the requested service class.
	/*!
		This implies a client has requested an operation that wasn't agreed upon at
		association negotiation.
	*/
	struct NoAvailableHandler:public dicom::exception
	{
		NoAvailableHandler():dicom::exception("No available handler."){}
	};


	//!Facilitates writing DICOM services
	/*!
		This class is responsible for listening on a socket for incoming
		TCP/IP connections.  On receiving a connection, it spawns an instance
		of ThreadSpecificServer in a new thread to handle that connection, and
		continues listening.  This way you can write a dicom service provider
		without having to manually implement any threading or socket code!

		All you need to do is to tell Server which function to call on receipt
		of a given dicom message, using AddHandler().

		This class should probably be a singleton, as I don't know what the
		behaviour would be if you create more than one.

		My goal is to make this the only class that needs to make use of
		mutexes - i.e. be responsible for all data that is shared between
		threads.

		Basic strategy is to have every PUBLIC member function mutex protected.


	*/
    class Server
		{
			friend struct Implementation::ThreadSpecificServer;

			Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		//!Only used if Server runs in a new thread.
		/*!
			Server can be run either in calling thread or in a new thread. If
			it is run in a new thread, the following member handles that thread.
			(in either case, it will spawn a thread for every incoming connection.)
		*/
		std::unique_ptr<std::thread> ServerThread_;

		//!do we want a mutex for every member, or just one?
		std::mutex mutex_;

		std::mutex killflag_mutex;

		//!When this is set to true, the Server will stop accepting new connections and eventually terminate.
		bool KillFlag;

		/*
			mutexes on stdout and stderr. Note that these won't help
			if you start more than one instance of Server.  Maybe
			we should make them static?
		*/

		static std::mutex cerr_mutex;
		static std::mutex cout_mutex;


		/*
			The following expressions associate abstract syntaxes, (e.g.
			UID_MAMMO_PRES_IMAGE_STORAGE_SOP_CLASS), with functions or functors.
			On receiving a given command, the appropriate handler function will
			be called.  THis allows the user to specify a handler, is type safe,
			and (I think) is less messy than using virtual functions.  Hopefully
			it will make thread-safety easier too.
			(This is a CALL BACK system.)

			This uses std::function to encapsulate functions-as-objects.
		*/


		std::map<UID,HandlerFunction> Handlers_;
			std::map<UID,CGetStatusFunction> CancellableGetHandlers_;
			std::map<UID,CMoveStatusFunction> CancellableMoveHandlers_;
			std::map<UID,CMoveStoreFunction> MoveStoreHandlers_;
			std::map<UID,NHandlerFunction> NEventReportHandlers_;
			std::map<UID,NAttributeHandlerFunction> NGetHandlers_;
			std::map<UID,NAttributeHandlerFunction> NSetHandlers_;
			std::map<UID,NHandlerFunction> NActionHandlers_;
			std::map<UID,NCreateAttributeHandlerFunction> NCreateHandlers_;
			std::map<UID,NHandlerFunction> NDeleteHandlers_;

		//!Abstract syntaxes accepted at association negotiation even without a handler.
		/*!
			Needed for Meta SOP Classes (e.g. Basic Grayscale Print Management Meta,
			PS3.4 Annex H): the SCU negotiates the Meta SOP Class as abstract syntax,
			but the DIMSE-N operations carried on that presentation context name the
			individual child SOP Classes in their Affected/Requested SOP Class UID.
			The Meta SOP Class itself has no operations, hence no handler.
		*/
		std::set<UID> AcceptableAbstractSyntaxes_;

		//!Identification announced at association negotiation; empty = library default.
		std::string ImplementationClassUIDOverride_;
		std::string ImplementationVersionNameOverride_;

		/*
			Would it be cleaner to explicitly specify CMoveHandlers_, CGetHandlers_ etc,
			i.e. one container for each C-DIMSE and N-DIMSE message?
		*/

		std::map<UID,CFindFunction> FindHandlers_;
		std::map<UID,CFindStatusFunction> CancellableFindHandlers_;

		//!Will be called to validate Local AETs.
		/*!
			Must be set by SetCheckLocalAETFunction() before calling Serve()
		*/
		StringCheckFunction CheckLocalAET;

		//!Will be called to validate Remote AETs.
		/*!
			Must be set by SetCheckRemoteAETFunction() before calling Serve()
		*/
		StringCheckFunction2 CheckRemoteAET;

		MoveDestinationResolverFunction ResolveMoveDestinationCallback_;
		SOPClassExtendedNegotiationFunction SOPClassExtendedNegotiationCallback_;

		//!Mutex for AET check functions.
		/*!
			 Probably not really needed, as they should only be set before we start serving.
		*/
		std::mutex AETMutex_;

		//!returns true if a client has requested a connection, false if Kill flag has been raised, otherwise blocks.
		bool ClientConnectionPending(Network::Socket* pSocket);


        //!Our own thread collection type.
        /*!
            We do not use a thread group here, because we need to be able to clean up threads as we go
            along, not just at the end of the program.
            This in turn requires us to carefully manage a boolean 'thread finished' flag, that is set in the client
            thread and detected in the main thread.


        */

        typedef std::map<std::shared_ptr<std::thread>,bool> ThreadGroup;

        //!Each thread owns an open socket connection to a client
        ThreadGroup clientThreads_;

        //!Nombre maximal d'associations traitées SIMULTANÉMENT (0 = illimité).
        /*!
            Un SCP Print manipule des images de plusieurs dizaines de Mo : chaque
            association concurrente en détient une pendant tout son traitement.
            Sans borne, N associations simultanées demandent N fois cette taille,
            et sur une carte embarquée le noyau finit par tuer le processus — ou
            la carte redémarre. Mesuré : 5 impressions simultanées d'une image
            4310×5312 RGB (68,7 Mo) ont fait tomber une carte de 963 Mo, alors
            que les mêmes 5 impressions en série tiennent dans 38 Mo.

            Refuser une association de plus est le comportement PRÉVU par la
            norme, et il est transitoire : le SCU réessaie.
        */
        size_t maxConcurrentAssociations_ = 0;
        size_t activeAssociations_ = 0;

        //!Un processus fils par association (cf. SetForkPerAssociation).
        bool forkPerAssociation_ = false;
        //!Nombre de processus fils vivants (mode fork).
        size_t liveChildren_ = 0;

        //!Récolte les fils terminés (waitpid non bloquant) — évite les zombies.
        void reapFinishedChildren();

        //!Attend qu'une place se libère avant d'accepter une association de plus.
        /*!
            EN MODE FORK, C'EST LE PÈRE QUI DOIT COMPTER.

            La limite est d'abord passée par TryBeginAssociation(), appelé depuis
            la négociation — donc dans le FILS. Or le compteur vit dans la mémoire
            du père : chaque fils incrémentait sa propre copie, invisible du père
            et des autres. La limite ne bornait donc rien du tout, et N images de
            plusieurs dizaines de Mo étaient traitées en parallèle. Mesuré : la
            carte redémarre.

            Le père attend ici qu'un fils se termine. Les connexions en attente
            patientent dans le backlog du noyau — un SCU qui patiente vaut mieux
            qu'une carte qui tombe.
        */
        void waitForChildSlot();

        //!true si une association de plus peut être acceptée ; la comptabilise.
        bool TryBeginAssociation();
        //!Libère un jeton pris par TryBeginAssociation().
        void EndAssociation();

        //!Does housekeeping work on clientThreads_
        void threadCleanup(bool cleanAll);


	public:

        //!Traite chaque association dans un PROCESSUS FILS plutôt qu'un thread.
        /*!
            Ce mode est destiné aux SCP autonomes qui traitent de gros objets
            par association et qui doivent rendre toute la mémoire au système à
            la fin de chaque association. À la sortie d'un processus fils, le
            noyau récupère la mémoire du traitement sans dépendre du comportement
            de l'allocateur du processus père.

            ⚠ NE PAS activer depuis un processus multithreadé sans `exec()` : le
            fils n'hérite que du thread appelant mais de TOUS les mutex dans leur
            état du moment — un verrou tenu par un autre thread reste verrouillé
            à jamais dans le fils. Ce mode vise le SCP autonome.

            Tout état qui doit survivre à une association doit vivre hors du
            processus fils ou être propagé explicitement au père.
        */
        void SetForkPerAssociation(bool enabled);

        //!Borne le nombre d'associations simultanées (0 = illimité, défaut).
        /*!
            À poser AVANT Serve(). Au-delà de la limite, l'association est
            refusée par A-ASSOCIATE-RJ « rejected-transient / local-limit-exceeded »
            (PS3.8 §9.3.4, Table 9-21 : Result 2, Source 3, Reason 2), ce qui
            invite le SCU à réessayer plus tard plutôt qu'à abandonner.
        */
        void SetMaxConcurrentAssociations(size_t maximum);

        //!signal to server object that current thread is free to be cleaned up
        void allDone();


		//!Logging facilities.
		/*!
			By supplying a class derived from Logger to SetLogger(), you can provide
			custom logging facilities.  (For example, in scippy we provide a class that redirects
			all log messages to a database table.)
		*/
		struct Logger
		{
			//!Defaults to writing to cerr
			virtual void LogError				(std::string Error);
			//!Defaults to writing to cout
			virtual void LogMessage				(std::string Message);
			//!Defaults to zero
			virtual void  AssociationNegotiated	(const primitive::AAssociateRQ& request){(void)request;}
			//!Defaults to nothing
			virtual void AssociationTerminated	(){}
			//!Called after a DIMSE operation has been handled AND its response written.
			/*!
				The only point at which an SCP may initiate a new message on the
				association without interleaving it with the response the SCU is
				waiting for — needed to send Print Management N-EVENT-REPORT
				(PS3.4 Annex H). `command` is the request command field (Tag.hpp
				Command::* codes). Defaults to nothing.
			*/
			virtual void OperationHandled		(ServiceBase& service, UINT16 command)
				{(void)service;(void)command;}

		}DefaultLogger_;

	private:
		Logger* CurrentLogger_;
	public:

		//!Call this to replace the default logger with a custom one.
		void SetLogger(Logger* logger);

		//!Pipes error message to current logger.
		void LogError(std::string Error);

		//!Pipes message to current logger
		void LogMessage(std::string Message);

		void SetCheckLocalAETCallback(StringCheckFunction f);
		void SetCheckRemoteAETCallback(StringCheckFunction2 f);
		void SetMoveDestinationResolverCallback(MoveDestinationResolverFunction f);
		void SetSOPClassExtendedNegotiationCallback(SOPClassExtendedNegotiationFunction f);

		void AddHandler(const UID& uid,HandlerFunction f);
		void AddFindHandler(const UID& uid,CFindFunction Handler);
		void AddCancellableFindHandler(const UID& uid,CFindStatusFunction Handler);
			void AddCancellableGetHandler(const UID& uid,CGetStatusFunction Handler);
			void AddCancellableMoveHandler(const UID& uid,CMoveStatusFunction Handler);
			void AddMoveStoreHandler(const UID& uid,CMoveStoreFunction Handler);
			void AddNEventReportHandler(const UID& uid,NHandlerFunction Handler);
			void AddNGetHandler(const UID& uid,NHandlerFunction Handler);
			void AddNGetHandler(const UID& uid,NAttributeHandlerFunction Handler);
			void AddNSetHandler(const UID& uid,NHandlerFunction Handler);
			void AddNSetHandler(const UID& uid,NAttributeHandlerFunction Handler);
			void AddNActionHandler(const UID& uid,NHandlerFunction Handler);
			void AddNCreateHandler(const UID& uid,NHandlerFunction Handler);
			void AddNCreateHandler(const UID& uid,NCreateHandlerFunction Handler);
			void AddNCreateHandler(const UID& uid,NCreateAttributeHandlerFunction Handler);
			void AddNDeleteHandler(const UID& uid,NHandlerFunction Handler);

			//!Accept this abstract syntax at negotiation even though it has no handler.
			/*!
				For Meta SOP Classes, whose operations are dispatched on the child
				SOP Class UID carried in the command set.
			*/
			void AddAcceptableAbstractSyntax(const UID& uid);

		HandlerFunction GetHandler(const UID& uid);
		CFindFunction GetFindHandler(const UID& uid);
		CFindStatusFunction GetCancellableFindHandler(const UID& uid);
		bool HasCancellableFindHandler(const UID& uid);
		CGetStatusFunction GetCancellableGetHandler(const UID& uid);
		bool HasCancellableGetHandler(const UID& uid);
			CMoveStatusFunction GetCancellableMoveHandler(const UID& uid);
			bool HasCancellableMoveHandler(const UID& uid);
			CMoveStoreFunction GetMoveStoreHandler(const UID& uid);
			bool HasMoveStoreHandler(const UID& uid);
			NHandlerFunction GetNEventReportHandler(const UID& uid);
			NAttributeHandlerFunction GetNGetHandler(const UID& uid);
			NAttributeHandlerFunction GetNSetHandler(const UID& uid);
			NHandlerFunction GetNActionHandler(const UID& uid);
			NCreateAttributeHandlerFunction GetNCreateHandler(const UID& uid);
			NHandlerFunction GetNDeleteHandler(const UID& uid);

		bool IsAcceptableRemoteApplicationTitle		(const std::string& Title,std::string ip);
		bool IsAcceptableLocalApplicationTitle		(const std::string& Title);
		bool ResolveMoveDestination					(const std::string& Title,MoveDestinationEndpoint& endpoint);
		bool NegotiateSOPClassExtended				(const UID& uid,const std::vector<BYTE>& request,std::vector<BYTE>& response);
		bool IsAcceptableApplicationContext			(const UID& uid);
		bool IsAcceptableAbstractSyntax				(const UID& uid);


		//for callback mechanism

		//it's possible we should make these protected members only accesible to
		//ThreadSpecificServer, using the friend keyword.
		void AssociationNegotiated					(const primitive::AAssociateRQ& request);
		void AssociationTerminated					();
		void OperationHandled						(ServiceBase& service,UINT16 command);

		bool CanHandleTransferSyntax				(primitive::TransferSyntax &);

		//these next two seem poorly thought out.
		void GetImplementationClass					(primitive::ImplementationClass &);
		void GetImplementationVersion				(primitive::ImplementationVersion &);

		//!Override the Implementation Class UID announced at association negotiation.
		/*!
			PS3.7 D.3.3.2: the Implementation Class UID identifies the implementation,
			and each implementor is expected to announce a UID under their OWN
			registered org root. Without this, dicomlib announces its own value,
			which belongs to the library author rather than to the application.
			Pass an empty string to fall back to the library default.
		*/
		void SetImplementationClassUID				(const std::string& uid);
		//!Override the (optional) Implementation Version Name.
		//!Empty string restores the default; non-empty values must be 1-16 printable
		//!ISO 646 characters.
		void SetImplementationVersionName			(const std::string& name);

		//!Constructor
		Server();

		//!Destructor.
		virtual ~Server();

		//!Does not return until SIG_TERM is received.
		void Serve(short Port);

		//!alias for Serve()
		void ServeInThisThread(short Port){Serve(Port);}

		//!Only call this once.
		void ServeInNewThread(short port);

		//!Only call this subsequent to calling ServeInNewThread;
		void Stop();

		//!Warn all threads to terminate cleanly
		void RaiseKillFlag();

		//!Has RaiseKillFlag been called
		bool KillFlagRaised();

	};
}//namespace dicom
#endif //SERVER_HPP_INCLUDE_GUARD_26510884
