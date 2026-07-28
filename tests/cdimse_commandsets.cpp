#include "dicomlib/Cdimse.hpp"
#include "dicomlib/aaac.hpp"
#include "dicomlib/ClientConnection.hpp"
#include "dicomlib/CommandSets.hpp"
#include "dicomlib/ImplementationUID.hpp"
#include "dicomlib/PresentationContexts.hpp"
#include "dicomlib/Server.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <exception>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace
{
	template <typename T>
	T get(const dicom::DataSet& data, const dicom::Tag& tag)
	{
		T value;
		data(tag) >> value;
		return value;
	}

	struct NullService : public dicom::ServiceBase
	{
		Network::Socket* GetSocket()
		{
			return 0;
		}
	};

	class PairedSocket : public Network::Socket
	{
		SOCKET fd_;
	public:
		explicit PairedSocket(SOCKET fd)
		: fd_(fd)
		{
		}

		~PairedSocket()
		{
			if(fd_ >= 0)
				::close(fd_);
		}

		SOCKET GetSocketDescriptor() const
		{
			return fd_;
		}

		std::string get_remote_ip() const
		{
			return "";
		}
	};

	struct PairedService : public dicom::ServiceBase
	{
		PairedSocket socket_;

		PairedService(SOCKET fd, const dicom::UID& classUID)
		: socket_(fd)
		{
			dicom::PresentationContexts contexts;
			contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
			AAssociateRQ_.ProposedPresentationContexts_ = contexts;
			AAssociateRQ_.UserInfo_.MaxSubLength_.Set(16384);

			dicom::primitive::PresentationContextAccept accepted;
			accepted.PresentationContextID_ = contexts.at(0).ID_;
			accepted.Result_ = 0;
			accepted.TrnSyntax_ = dicom::primitive::TransferSyntax(dicom::IMPL_VR_LE_TRANSFER_SYNTAX);
			AcceptedPresentationContexts_.push_back(accepted);
		}

		Network::Socket* GetSocket()
		{
			return &socket_;
		}
	};

	void configureAssociation(
		PairedService& service,
		const dicom::PresentationContexts& contexts,
		const std::vector<dicom::primitive::PresentationContextAccept>& accepted)
	{
		service.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		service.AAssociateRQ_.UserInfo_.MaxSubLength_.Set(16384);
		service.AcceptedPresentationContexts_ = accepted;
	}

	void configureAssociation(
		PairedService& service,
		const dicom::PresentationContexts& contexts,
		const dicom::primitive::AAssociateAC& acknowledgement)
	{
		configureAssociation(service, contexts, acknowledgement.PresContextAccepts_);
	}

	dicom::primitive::PresentationContextAccept acceptFirstContext(
		const dicom::PresentationContexts& contexts,
		const dicom::UID& transferSyntax)
	{
		dicom::primitive::PresentationContextAccept accepted;
		accepted.PresentationContextID_ = contexts.at(0).ID_;
		accepted.Result_ = 0;
		accepted.TrnSyntax_ = dicom::primitive::TransferSyntax(transferSyntax);
		return accepted;
	}

	void negotiateAssociation(
		PairedService& scuSide,
		PairedService& scpSide,
		const dicom::UID& classUID)
	{
		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		dicom::primitive::MaximumSubLength maxSubLength;
		maxSubLength.Set(16384);

		dicom::primitive::UserInformation userInfo;
		userInfo.ImpClass_.UID_ = dicom::ImplementationClassUID;
		userInfo.ImpVersion_.Name = dicom::ImplementationVersionName;
		userInfo.SetMax(maxSubLength);

		scuSide.AAssociateRQ_.CalledAppTitle_ = "SCP_AE";
		scuSide.AAssociateRQ_.CallingAppTitle_ = "SCU_AE";
		scuSide.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		scuSide.AAssociateRQ_.SetUserInformation(userInfo);
		scuSide.AAssociateRQ_.Write(*scuSide.GetSocket());

		dicom::primitive::AAssociateRQ request;
		request.Read(*scpSide.GetSocket());
		assert(request.CalledAppTitle_ == "SCP_AE");
		assert(request.CallingAppTitle_ == "SCU_AE");
		assert(request.ProposedPresentationContexts_.size() == 1);
		assert(request.ProposedPresentationContexts_.at(0).AbsSyntax_.UID_ == classUID);

		dicom::primitive::PresentationContextAccept accepted =
			acceptFirstContext(contexts, dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		dicom::primitive::AAssociateAC acknowledgement;
		acknowledgement.CalledAppTitle_ = request.CalledAppTitle_;
		acknowledgement.CallingAppTitle_ = request.CallingAppTitle_;
		acknowledgement.AppContext_ = request.AppContext_;
		acknowledgement.PresContextAccepts_.push_back(accepted);
		acknowledgement.SetUserInformation(userInfo);
		acknowledgement.Write(*scpSide.GetSocket());

		dicom::primitive::AAssociateAC readAcknowledgement;
		readAcknowledgement.Read(*scuSide.GetSocket());
		assert(readAcknowledgement.PresContextAccepts_.size() == 1);
		assert(readAcknowledgement.PresContextAccepts_.at(0).Result_ == 0);
		assert(readAcknowledgement.PresContextAccepts_.at(0).TrnSyntax_.UID_ == dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		std::vector<dicom::primitive::PresentationContextAccept> acceptedContexts;
		acceptedContexts.push_back(accepted);
		configureAssociation(scpSide, contexts, acceptedContexts);
		configureAssociation(scuSide, contexts, readAcknowledgement);
	}

	struct TestCFindSCU : public dicom::CFindSCU
	{
		TestCFindSCU(dicom::ServiceBase& service, const dicom::UID& classUID)
		: dicom::CFindSCU(service, classUID)
		{
		}

		void setLastMessageID(UINT16 messageID)
		{
			lastMessageID_ = messageID;
		}
	};

	void makeSocketPair(int sockets[2])
	{
		const int result = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
		if(result != 0)
			throw SystemError("socketpair");
	}

	short reserveLocalPort()
	{
		const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
		if(fd < 0)
			throw SystemError("socket");

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = 0;
		if(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
		{
			const SystemError error("bind");
			::close(fd);
			throw error;
		}

		socklen_t length = sizeof(address);
		if(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
		{
			const SystemError error("getsockname");
			::close(fd);
			throw error;
		}
		const short port = static_cast<short>(ntohs(address.sin_port));
		::close(fd);
		return port;
	}

	bool acceptAnyLocalAET(const std::string&)
	{
		return true;
	}

	bool acceptAnyRemoteAET(const std::string&, const std::string&)
	{
		return true;
	}

	struct QuietLogger : public dicom::Server::Logger
	{
		void LogError(std::string)
		{
		}

		void LogMessage(std::string)
		{
		}
	};

	struct CancelDispatchLogger : public QuietLogger
	{
		std::atomic<bool>& handledCancel_;

		explicit CancelDispatchLogger(std::atomic<bool>& handledCancel)
		: handledCancel_(handledCancel)
		{
		}

		void LogMessage(std::string message)
		{
			if(message == "Handled a C-CANCEL-RQ")
				handledCancel_ = true;
		}
	};

	void checkCEcho()
	{
		const dicom::UID classUID("1.2.840.10008.1.1");
		dicom::CommandSet::CEchoRQ rq(1, classUID);
		assert(get<dicom::UID>(rq, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<UINT16>(rq, dicom::TAG_CMD_FIELD) == dicom::Command::C_ECHO_RQ);
		assert(get<UINT16>(rq, dicom::TAG_MSG_ID) == 1);
		assert(get<UINT16>(rq, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);

		dicom::CommandSet::CEchoRSP rsp(1, classUID);
		assert(get<UINT16>(rsp, dicom::TAG_CMD_FIELD) == dicom::Command::C_ECHO_RSP);
		assert(get<UINT16>(rsp, dicom::TAG_MSG_ID_RSP) == 1);
		assert(get<UINT16>(rsp, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
		assert(get<UINT16>(rsp, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
	}

	void checkCStore()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.1.2");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1");
		dicom::CommandSet::CStoreRQ rq(3, classUID, instUID, dicom::Priority::HIGH);
		assert(get<dicom::UID>(rq, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(rq, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(rq, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RQ);
		assert(get<UINT16>(rq, dicom::TAG_MSG_ID) == 3);
		assert(get<UINT16>(rq, dicom::TAG_PRIORITY) == dicom::Priority::HIGH);
		assert(get<UINT16>(rq, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);

		dicom::CommandSet::CStoreRSP rsp(3, classUID, instUID, dicom::Status::SUCCESS);
		assert(get<UINT16>(rsp, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RSP);
		assert(get<UINT16>(rsp, dicom::TAG_MSG_ID_RSP) == 3);
		assert(get<dicom::UID>(rsp, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(rsp, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
	}

	void checkCFind()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.1");
		dicom::CommandSet::CFindRQ rq(5, classUID, dicom::Priority::LOW);
		assert(get<UINT16>(rq, dicom::TAG_CMD_FIELD) == dicom::Command::C_FIND_RQ);
		assert(get<UINT16>(rq, dicom::TAG_MSG_ID) == 5);
		assert(get<UINT16>(rq, dicom::TAG_PRIORITY) == dicom::Priority::LOW);
		assert(get<UINT16>(rq, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);

		dicom::CommandSet::CFindRSP rsp(5, classUID, dicom::Status::PENDING, dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(rsp, dicom::TAG_CMD_FIELD) == dicom::Command::C_FIND_RSP);
		assert(get<UINT16>(rsp, dicom::TAG_MSG_ID_RSP) == 5);
		assert(get<UINT16>(rsp, dicom::TAG_STATUS) == dicom::Status::PENDING);
		assert(get<UINT16>(rsp, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
	}

	void checkCGet()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.3");
		dicom::CommandSet::CGetRQ rq(7, classUID);
		assert(get<UINT16>(rq, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RQ);
		assert(get<UINT16>(rq, dicom::TAG_MSG_ID) == 7);
		assert(get<UINT16>(rq, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);

		dicom::CommandSet::CGetRSP rsp(7, classUID, dicom::Status::PENDING, dicom::DataSetStatus::NO_DATA_SET);
		rsp.setRemaining(2);
		rsp.setCompleted(1);
		rsp.setFailed(0);
		rsp.setWarning(0);
		assert(get<UINT16>(rsp, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RSP);
		assert(get<UINT16>(rsp, dicom::TAG_NUM_REMAIN_SUBOP) == 2);
		assert(get<UINT16>(rsp, dicom::TAG_NUM_COMPL_SUBOP) == 1);
		assert(get<UINT16>(rsp, dicom::TAG_NUM_FAIL_SUBOP) == 0);
		assert(get<UINT16>(rsp, dicom::TAG_NUM_WARN_SUBOP) == 0);
	}

	void checkCdimseStatusHelpers()
	{
		assert(dicom::IsCdimseSuccessStatus(dicom::Status::SUCCESS));
		assert(!dicom::IsCdimseSuccessStatus(dicom::Status::PENDING));

		assert(dicom::IsCdimsePendingStatus(dicom::Status::PENDING));
		assert(dicom::IsCdimsePendingStatus(dicom::Status::PENDING1));
		assert(!dicom::IsCdimsePendingStatus(dicom::Status::SUCCESS));

		assert(dicom::IsCdimseCancelStatus(dicom::Status::CANCEL));
		assert(!dicom::IsCdimseCancelStatus(dicom::Status::SUCCESS));

		assert(dicom::IsCdimseWarningStatus(dicom::Status::WARNING));
		assert(!dicom::IsCdimseWarningStatus(dicom::Status::SUCCESS));

		assert(!dicom::IsCdimseFinalStatus(dicom::Status::PENDING));
		assert(!dicom::IsCdimseFinalStatus(dicom::Status::PENDING1));
		assert(dicom::IsCdimseFinalStatus(dicom::Status::SUCCESS));
		assert(dicom::IsCdimseFinalStatus(dicom::Status::CANCEL));
		assert(dicom::IsCdimseFinalStatus(dicom::Status::WARNING));

		assert(dicom::IsCEchoResponseStatus(dicom::Status::SUCCESS));
		assert(dicom::IsCEchoResponseStatus(0x0122));
		assert(dicom::IsCEchoResponseStatus(0x0210));
		assert(dicom::IsCEchoResponseStatus(0x0211));
		assert(dicom::IsCEchoResponseStatus(0x0212));
		assert(!dicom::IsCEchoResponseStatus(dicom::Status::WARNING));
		assert(!dicom::IsCEchoResponseStatus(dicom::Status::PENDING));

		assert(dicom::IsCStoreResponseStatus(dicom::Status::SUCCESS));
		assert(dicom::IsCStoreResponseStatus(0xb000));
		assert(dicom::IsCStoreResponseStatus(0xb006));
		assert(dicom::IsCStoreResponseStatus(0xb007));
		assert(dicom::IsCStoreResponseStatus(0xa700));
		assert(dicom::IsCStoreResponseStatus(0xa7ff));
		assert(dicom::IsCStoreResponseStatus(0xa900));
		assert(dicom::IsCStoreResponseStatus(0xa9ff));
		assert(dicom::IsCStoreResponseStatus(0xc123));
		assert(!dicom::IsCStoreResponseStatus(dicom::Status::CANCEL));
		assert(!dicom::IsCStoreResponseStatus(dicom::Status::PENDING));

		assert(dicom::IsCFindResponseStatus(dicom::Status::SUCCESS));
		assert(dicom::IsCFindResponseStatus(dicom::Status::CANCEL));
		assert(dicom::IsCFindResponseStatus(dicom::Status::PENDING));
		assert(dicom::IsCFindResponseStatus(dicom::Status::PENDING1));
		assert(dicom::IsCFindResponseStatus(0xa700));
		assert(dicom::IsCFindResponseStatus(0xa900));
		assert(dicom::IsCFindResponseStatus(0xc123));
		assert(!dicom::IsCFindResponseStatus(dicom::Status::WARNING));
		assert(!dicom::IsCFindResponseStatus(0xa801));

		assert(dicom::IsCGetResponseStatus(dicom::Status::SUCCESS));
		assert(dicom::IsCGetResponseStatus(dicom::Status::CANCEL));
		assert(dicom::IsCGetResponseStatus(dicom::Status::WARNING));
		assert(dicom::IsCGetResponseStatus(dicom::Status::PENDING));
		assert(dicom::IsCGetResponseStatus(0xa701));
		assert(dicom::IsCGetResponseStatus(0xa702));
		assert(dicom::IsCGetResponseStatus(0xa900));
		assert(dicom::IsCGetResponseStatus(0xc123));
		assert(!dicom::IsCGetResponseStatus(dicom::Status::PENDING1));
		assert(!dicom::IsCGetResponseStatus(0xa801));

		assert(dicom::IsCMoveResponseStatus(dicom::Status::SUCCESS));
		assert(dicom::IsCMoveResponseStatus(dicom::Status::CANCEL));
		assert(dicom::IsCMoveResponseStatus(dicom::Status::WARNING));
		assert(dicom::IsCMoveResponseStatus(dicom::Status::PENDING));
		assert(dicom::IsCMoveResponseStatus(0xa701));
		assert(dicom::IsCMoveResponseStatus(0xa702));
		assert(dicom::IsCMoveResponseStatus(0xa801));
		assert(dicom::IsCMoveResponseStatus(0xa900));
		assert(dicom::IsCMoveResponseStatus(0xc123));
		assert(!dicom::IsCMoveResponseStatus(dicom::Status::PENDING1));
		assert(!dicom::IsCMoveResponseStatus(0xa700));
	}

	void checkCCancel()
	{
		dicom::CommandSet::CCancelRQ rq(7);
		assert(get<UINT16>(rq, dicom::TAG_CMD_FIELD) == dicom::Command::C_CANCEL_RQ);
		assert(get<UINT16>(rq, dicom::TAG_MSG_ID_RSP) == 7);
		assert(get<UINT16>(rq, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		assert(!rq.exists(dicom::TAG_AFF_SOP_CLASS_UID));
		assert(!rq.exists(dicom::TAG_REQ_SOP_CLASS_UID));

		NullService service;
		assert(!service.IsCancelRequested(7));
		dicom::HandleCCancel(service, rq);
		assert(service.IsCancelRequested(7));
		service.ClearCancelRequest(7);
		assert(!service.IsCancelRequested(7));

		dicom::CommandSet::CCancelRQ invalidMessageID(0);
		bool rejectedMessageID = false;
		try
		{
			dicom::HandleCCancel(service, invalidMessageID);
		}
		catch(const std::exception&)
		{
			rejectedMessageID = true;
		}
		assert(rejectedMessageID);

		dicom::DataSet invalidDataSetType;
		invalidDataSetType.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_CANCEL_RQ);
		invalidDataSetType.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(7));
		invalidDataSetType.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		bool rejectedDataSetType = false;
		try
		{
			dicom::HandleCCancel(service, invalidDataSetType);
		}
		catch(const std::exception&)
		{
			rejectedDataSetType = true;
		}
		assert(rejectedDataSetType);
	}

	void checkCCancelOverPData()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.1");
		int sockets[2];
		makeSocketPair(sockets);
		PairedService scuSide(sockets[0], classUID);
		PairedService scpSide(sockets[1], classUID);

		TestCFindSCU scu(scuSide, classUID);
		scu.setLastMessageID(11);
		scu.writeCancelRQ();

		dicom::DataSet command;
		const bool readCancel = scpSide.Read(command);
		assert(readCancel);
		assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_CANCEL_RQ);
		assert(!scpSide.IsCancelRequested(11));
		dicom::HandleCCancel(scpSide, command);
		assert(scpSide.IsCancelRequested(11));
	}

	void checkSCUResponseValidationOverPData()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.1");
		int sockets[2];
		makeSocketPair(sockets);
		PairedService scuSide(sockets[0], classUID);
		PairedService scpSide(sockets[1], classUID);

		TestCFindSCU scu(scuSide, classUID);
		scu.setLastMessageID(13);

		dicom::CommandSet::CFindRSP wrongMessageID(
			15,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		scpSide.WriteCommand(wrongMessageID, classUID);

		UINT16 status = 0;
		dicom::DataSet response;
		dicom::DataSet data;
		bool rejected = false;
		try
		{
			scu.readRSP(status, response, data);
		}
		catch(const std::exception&)
		{
			rejected = true;
		}
		assert(rejected);
	}

	void checkAssociationNegotiationAndCEcho()
	{
		const dicom::UID classUID("1.2.840.10008.1.1");
		int sockets[2];
		makeSocketPair(sockets);
		PairedService scuSide(sockets[0], classUID);
		PairedService scpSide(sockets[1], classUID);

		negotiateAssociation(scuSide, scpSide, classUID);

		dicom::CEchoSCU echoSCU(scuSide);
		echoSCU.writeRQ();

		dicom::DataSet command;
		const bool readEcho = scpSide.Read(command);
		assert(readEcho);
		assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_ECHO_RQ);
		dicom::HandleCEcho(scpSide, command, classUID);

		UINT16 status = 0;
		dicom::DataSet response;
		echoSCU.readRSP(status, response);
		assert(status == dicom::Status::SUCCESS);
	}

	void checkServerClientCEcho()
	{
		const short port = reserveLocalPort();
		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(dicom::VERIFICATION_SOP_CLASS, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::DataSet response = client.Echo();
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_ECHO_RSP);
				assert(get<UINT16>(response, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
	}

	void checkServerClientCStore()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::SC_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.1");
		std::atomic<bool> handled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddHandler(
			classUID,
			[&](dicom::ServiceBase&, const dicom::DataSet& command, dicom::DataSet& data)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RQ);
				assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
				assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_INST_UID) == instanceUID);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_CLASS_UID) == classUID);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == instanceUID);
				handled = true;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet instance;
				instance.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, classUID);
				instance.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instanceUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::DataSet response = client.Store(instance);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RSP);
				assert(get<UINT16>(response, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
				assert(get<dicom::UID>(response, dicom::TAG_AFF_SOP_INST_UID) == instanceUID);
				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handled);
	}

	void checkServerClientCFind()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_FIND_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.2");
		std::atomic<bool> handled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddFindHandler(
			classUID,
			[&](dicom::ServiceBase&, dicom::DataSet& query, dicom::Sequence& matches)
			{
				assert(get<std::string>(query, dicom::TAG_QR_LEVEL) == "STUDY");

				dicom::DataSet match;
				match.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				match.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
				matches.push_back(match);
				handled = true;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				std::vector<dicom::DataSet> responses =
					client.Find(query, dicom::QueryRetrieve::STUDY_ROOT);
				assert(responses.size() == 1);
				assert(get<std::string>(responses.at(0), dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(responses.at(0), dicom::TAG_STUDY_INST_UID) == studyUID);
				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handled);
	}

	void checkServerClientCMove()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_MOVE_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.3");
		std::atomic<bool> handled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddHandler(
			classUID,
			[&](dicom::ServiceBase& service, const dicom::DataSet& command, dicom::DataSet& request)
			{
				const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RQ);
				assert(get<std::string>(command, dicom::TAG_MOVE_DEST) == "DEST_AE");
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(request, dicom::TAG_STUDY_INST_UID) == studyUID);

				dicom::CommandSet::CMoveRSP response(
					messageID,
					classUID,
					dicom::Status::SUCCESS,
					dicom::DataSetStatus::NO_DATA_SET);
				response.setCompleted(0);
				response.setFailed(0);
				response.setWarning(0);
				service.WriteCommand(response, classUID);
				handled = true;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::DataSet response =
					client.Move("DEST_AE", query, dicom::QueryRetrieve::STUDY_ROOT);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RSP);
				assert(get<UINT16>(response, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handled);
	}

	void checkServerClientCMoveStoreSubOperationScheduler()
	{
		const short movePort = reserveLocalPort();
		short destinationPort = reserveLocalPort();
		while(destinationPort == movePort)
			destinationPort = reserveLocalPort();
		const dicom::UID moveClassUID = dicom::STUDY_ROOT_QR_MOVE_SOP_CLASS;
		const dicom::UID storeClassUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.8");
		const dicom::UID firstInstanceUID("1.2.826.0.1.3680043.10.1553.8.1");
		const dicom::UID secondInstanceUID("1.2.826.0.1.3680043.10.1553.8.2");
		std::atomic<bool> moveHandled(false);
		std::atomic<int> destinationStores(0);
		std::atomic<int> moveOriginatorMessageID(0);

		QuietLogger destinationLogger;
		dicom::Server destinationServer;
		destinationServer.SetLogger(&destinationLogger);
		destinationServer.SetCheckLocalAETCallback(acceptAnyLocalAET);
		destinationServer.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		destinationServer.AddHandler(
			storeClassUID,
			[&](dicom::ServiceBase&, const dicom::DataSet& command, dicom::DataSet& stored)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RQ);
				assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_CLASS_UID) == storeClassUID);
				assert(get<std::string>(command, dicom::TAG_MOVE_ORIG_AET) == "SCU_AE");
				assert(get<UINT16>(command, dicom::TAG_MOVE_ORIG_MSG_ID) == moveOriginatorMessageID.load());
				assert(get<dicom::UID>(stored, dicom::TAG_SOP_CLASS_UID) == storeClassUID);
				assert(get<dicom::UID>(stored, dicom::TAG_STUDY_INST_UID) == studyUID);
				destinationStores.fetch_add(1);
			});
		destinationServer.ServeInNewThread(destinationPort);

		QuietLogger moveLogger;
		dicom::Server moveServer;
		moveServer.SetLogger(&moveLogger);
		moveServer.SetCheckLocalAETCallback(acceptAnyLocalAET);
		moveServer.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		moveServer.SetMoveDestinationResolverCallback(
			[&](const std::string& title, dicom::MoveDestinationEndpoint& endpoint)
			{
				if(title != "DEST_AE")
					return false;
				endpoint = dicom::MoveDestinationEndpoint("127.0.0.1", destinationPort);
				return true;
			});
		moveServer.AddMoveStoreHandler(
			moveClassUID,
			[&](
				dicom::ServiceBase&,
				const dicom::DataSet& command,
				dicom::DataSet& request,
				dicom::Sequence& instances,
				dicom::PresentationContexts& destinationContexts)
			{
				const UINT16 moveMessageID = get<UINT16>(command, dicom::TAG_MSG_ID);
				const std::string destinationAET = get<std::string>(command, dicom::TAG_MOVE_DEST);
				moveOriginatorMessageID = moveMessageID;
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RQ);
				assert(destinationAET == "DEST_AE");
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(request, dicom::TAG_STUDY_INST_UID) == studyUID);

				dicom::DataSet first;
				first.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				first.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, firstInstanceUID);
				first.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::DataSet second;
				second.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				second.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, secondInstanceUID);
				second.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				instances.push_back(first);
				instances.push_back(second);

				destinationContexts.Add(storeClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
				moveHandled = true;
			});
		moveServer.ServeInNewThread(movePort);

		dicom::PresentationContexts contexts;
		contexts.Add(moveClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", movePort, "SCU_AE", "MOVE_SCP", contexts);
				dicom::DataSet response =
					client.Move("DEST_AE", query, dicom::QueryRetrieve::STUDY_ROOT);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RSP);
				assert(get<UINT16>(response, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 2);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				completed = moveHandled && destinationStores == 2;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			catch(const std::exception&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		moveServer.Stop();
		destinationServer.Stop();
		assert(completed);
		assert(moveHandled);
		assert(destinationStores == 2);
	}

	void checkServerClientCMoveStoreSubOperationCancel()
	{
		const short movePort = reserveLocalPort();
		short destinationPort = reserveLocalPort();
		while(destinationPort == movePort)
			destinationPort = reserveLocalPort();
		const dicom::UID moveClassUID = dicom::STUDY_ROOT_QR_MOVE_SOP_CLASS;
		const dicom::UID storeClassUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.9");
		const dicom::UID firstInstanceUID("1.2.826.0.1.3680043.10.1553.9.1");
		const dicom::UID secondInstanceUID("1.2.826.0.1.3680043.10.1553.9.2");
		std::atomic<bool> moveHandled(false);
		std::atomic<int> destinationStores(0);
		std::atomic<int> moveOriginatorMessageID(0);

		QuietLogger destinationLogger;
		dicom::Server destinationServer;
		destinationServer.SetLogger(&destinationLogger);
		destinationServer.SetCheckLocalAETCallback(acceptAnyLocalAET);
		destinationServer.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		destinationServer.AddHandler(
			storeClassUID,
			[&](dicom::ServiceBase&, const dicom::DataSet& command, dicom::DataSet& stored)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RQ);
				assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_CLASS_UID) == storeClassUID);
				assert(get<std::string>(command, dicom::TAG_MOVE_ORIG_AET) == "SCU_AE");
				assert(get<UINT16>(command, dicom::TAG_MOVE_ORIG_MSG_ID) == moveOriginatorMessageID.load());
				assert(get<dicom::UID>(stored, dicom::TAG_SOP_CLASS_UID) == storeClassUID);
				assert(get<dicom::UID>(stored, dicom::TAG_STUDY_INST_UID) == studyUID);
				const int count = destinationStores.fetch_add(1) + 1;
				if(count == 1)
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
			});
		destinationServer.ServeInNewThread(destinationPort);

		QuietLogger moveLogger;
		dicom::Server moveServer;
		moveServer.SetLogger(&moveLogger);
		moveServer.SetCheckLocalAETCallback(acceptAnyLocalAET);
		moveServer.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		moveServer.SetMoveDestinationResolverCallback(
			[&](const std::string& title, dicom::MoveDestinationEndpoint& endpoint)
			{
				if(title != "DEST_AE")
					return false;
				endpoint = dicom::MoveDestinationEndpoint("127.0.0.1", destinationPort);
				return true;
			});
		moveServer.AddMoveStoreHandler(
			moveClassUID,
			[&](
				dicom::ServiceBase&,
				const dicom::DataSet& command,
				dicom::DataSet& request,
				dicom::Sequence& instances,
				dicom::PresentationContexts& destinationContexts)
			{
				const UINT16 moveMessageID = get<UINT16>(command, dicom::TAG_MSG_ID);
				const std::string destinationAET = get<std::string>(command, dicom::TAG_MOVE_DEST);
				moveOriginatorMessageID = moveMessageID;
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RQ);
				assert(destinationAET == "DEST_AE");
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(request, dicom::TAG_STUDY_INST_UID) == studyUID);

				dicom::DataSet first;
				first.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				first.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, firstInstanceUID);
				first.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::DataSet second;
				second.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				second.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, secondInstanceUID);
				second.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				instances.push_back(first);
				instances.push_back(second);
				destinationContexts.Add(storeClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
				moveHandled = true;
			});
		moveServer.ServeInNewThread(movePort);

		dicom::PresentationContexts contexts;
		contexts.Add(moveClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", movePort, "SCU_AE", "MOVE_SCP", contexts);
				dicom::CMoveSCU moveSCU(client, moveClassUID);
				moveSCU.writeRQ("DEST_AE", query);

				for(int wait = 0; wait < 20 && destinationStores.load() == 0; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				moveSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				moveSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 1);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 1);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				assert(data.empty());
				completed = moveHandled && destinationStores == 1;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			catch(const std::exception&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		moveServer.Stop();
		destinationServer.Stop();
		assert(completed);
		assert(moveHandled);
		assert(destinationStores == 1);
	}

	void checkServerClientCGet()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_GET_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.4");
		std::atomic<bool> handled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddHandler(
			classUID,
			[&](dicom::ServiceBase& service, const dicom::DataSet& command, dicom::DataSet& request)
			{
				const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RQ);
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(request, dicom::TAG_STUDY_INST_UID) == studyUID);

				dicom::CommandSet::CGetRSP response(
					messageID,
					classUID,
					dicom::Status::SUCCESS,
					dicom::DataSetStatus::NO_DATA_SET);
				response.setCompleted(0);
				response.setFailed(0);
				response.setWarning(0);
				service.WriteCommand(response, classUID);
				handled = true;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CGetSCU getSCU(client, classUID);
				getSCU.writeRQ(query);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				getSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RSP);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				assert(data.empty());
				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handled);
	}

	void checkServerClientCGetStoreSubOperation()
	{
		const short port = reserveLocalPort();
		const dicom::UID getClassUID = dicom::STUDY_ROOT_QR_GET_SOP_CLASS;
		const dicom::UID storeClassUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.6");
		const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.6.1");
		std::atomic<bool> getHandled(false);
		std::atomic<bool> storeHandled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddHandler(
			storeClassUID,
			[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
			{
			});
		server.AddCancellableGetHandler(
			getClassUID,
			[&](dicom::ServiceBase& service, const dicom::DataSet& command, dicom::DataSet& request)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RQ);
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(request, dicom::TAG_STUDY_INST_UID) == studyUID);

				dicom::DataSet stored;
				stored.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				stored.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instanceUID);
				stored.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::CStoreSCU storeSCU(service, storeClassUID);
				storeSCU.writeRQ(instanceUID, stored);

				UINT16 storeStatus = 0;
				dicom::DataSet storeResponse;
				storeSCU.readRSP(storeStatus, storeResponse);
				assert(get<UINT16>(storeResponse, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RSP);
				assert(storeStatus == dicom::Status::SUCCESS);

				getHandled = true;
				return dicom::CSubOperationResult(dicom::Status::SUCCESS, 0, 1, 0, 0);
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(getClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
		contexts.Add(storeClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CGetSCU getSCU(client, getClassUID);
				getSCU.writeRQ(query);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				getSCU.readRSP(
					status,
					response,
					data,
					[&](dicom::ServiceBase&, const dicom::DataSet& command, dicom::DataSet& stored)
					{
						assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RQ);
						assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_CLASS_UID) == storeClassUID);
						assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_INST_UID) == instanceUID);
						assert(get<dicom::UID>(stored, dicom::TAG_SOP_CLASS_UID) == storeClassUID);
						assert(get<dicom::UID>(stored, dicom::TAG_SOP_INST_UID) == instanceUID);
						assert(get<dicom::UID>(stored, dicom::TAG_STUDY_INST_UID) == studyUID);
						storeHandled = true;
					});

				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RSP);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 1);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				assert(data.empty());
				completed = getHandled && storeHandled;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(getHandled);
		assert(storeHandled);
	}

	void checkServerClientCGetStoreSubOperationScheduler()
	{
		const short port = reserveLocalPort();
		const dicom::UID getClassUID = dicom::STUDY_ROOT_QR_GET_SOP_CLASS;
		const dicom::UID storeClassUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.7");
		const dicom::UID firstInstanceUID("1.2.826.0.1.3680043.10.1553.7.1");
		const dicom::UID secondInstanceUID("1.2.826.0.1.3680043.10.1553.7.2");
		std::atomic<bool> getHandled(false);
		std::atomic<int> storeHandled(0);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddHandler(
			storeClassUID,
			[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
			{
			});
		server.AddCancellableGetHandler(
			getClassUID,
			[&](dicom::ServiceBase& service, const dicom::DataSet& command, dicom::DataSet& request)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RQ);
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				assert(get<dicom::UID>(request, dicom::TAG_STUDY_INST_UID) == studyUID);

				dicom::DataSet first;
				first.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				first.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, firstInstanceUID);
				first.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::DataSet second;
				second.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
				second.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, secondInstanceUID);
				second.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::Sequence instances;
				instances.push_back(first);
				instances.push_back(second);

				const dicom::CSubOperationResult result =
					dicom::SendCGetStoreSubOperations(service, instances);
				getHandled = true;
				return result;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(getClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
		contexts.Add(storeClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CGetSCU getSCU(client, getClassUID);
				getSCU.writeRQ(query);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				getSCU.readRSP(
					status,
					response,
					data,
					[&](dicom::ServiceBase&, const dicom::DataSet& command, dicom::DataSet& stored)
					{
						assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_STORE_RQ);
						assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_CLASS_UID) == storeClassUID);
						assert(get<dicom::UID>(stored, dicom::TAG_SOP_CLASS_UID) == storeClassUID);
						assert(get<dicom::UID>(stored, dicom::TAG_STUDY_INST_UID) == studyUID);
						storeHandled.fetch_add(1);
					});

				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RSP);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 2);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				assert(data.empty());
				completed = getHandled && storeHandled == 2;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(getHandled);
		assert(storeHandled == 2);
	}

	void checkServerClientCGetFinalCancelStatus()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_GET_SOP_CLASS;
		std::atomic<bool> handlerStarted(false);
		std::atomic<bool> cancelObserved(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddCancellableGetHandler(
			classUID,
			[&](dicom::ServiceBase& service, const dicom::DataSet& command, dicom::DataSet& request)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RQ);
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				handlerStarted = true;

				for(int wait = 0; wait < 20 && !cancelObserved; ++wait)
				{
					const bool observed = dicom::PollCCancelRQ(service);
					if(observed)
						cancelObserved = true;
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}

				return dicom::CSubOperationResult(
					cancelObserved ? dicom::Status::CANCEL : dicom::Status::SUCCESS,
					0,
					0,
					0,
					0);
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CGetSCU getSCU(client, classUID);
				getSCU.writeRQ(query);

				for(int wait = 0; wait < 20 && !handlerStarted; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				getSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				getSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_GET_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(get<UINT16>(response, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
				assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				assert(data.empty());
				completed = cancelObserved;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handlerStarted);
		assert(cancelObserved);
	}

	void checkServerClientCMoveFinalCancelStatus()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_MOVE_SOP_CLASS;
		std::atomic<bool> handlerStarted(false);
		std::atomic<bool> cancelObserved(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddCancellableMoveHandler(
			classUID,
			[&](dicom::ServiceBase& service, const dicom::DataSet& command, dicom::DataSet& request)
			{
				assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RQ);
				assert(get<std::string>(command, dicom::TAG_MOVE_DEST) == "DEST_AE");
				assert(get<std::string>(request, dicom::TAG_QR_LEVEL) == "STUDY");
				handlerStarted = true;

				for(int wait = 0; wait < 20 && !cancelObserved; ++wait)
				{
					const bool observed = dicom::PollCCancelRQ(service);
					if(observed)
						cancelObserved = true;
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}

				return dicom::CSubOperationResult(
					cancelObserved ? dicom::Status::CANCEL : dicom::Status::SUCCESS,
					0,
					0,
					0,
					0);
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CMoveSCU moveSCU(client, classUID);
				moveSCU.writeRQ("DEST_AE", query);

				for(int wait = 0; wait < 20 && !handlerStarted; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				moveSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				moveSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(get<UINT16>(response, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
				assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
				assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
				assert(data.empty());
				completed = cancelObserved;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handlerStarted);
		assert(cancelObserved);
	}

	void checkServerClientCCancelDispatch()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_FIND_SOP_CLASS;
		std::atomic<bool> findHandled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddFindHandler(
			classUID,
			[&](dicom::ServiceBase&, dicom::DataSet& query, dicom::Sequence& matches)
			{
				(void)matches;
				assert(get<std::string>(query, dicom::TAG_QR_LEVEL) == "STUDY");
				findHandled = true;
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CFindSCU findSCU(client, classUID);
				findSCU.writeRQ(query);
				findSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				findSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_FIND_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(data.empty());

				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(findHandled);
	}

	void checkServerClientCCancelObservedByRunningHandler()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_FIND_SOP_CLASS;
		std::atomic<bool> handlerStarted(false);
		std::atomic<bool> cancelObserved(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddFindHandler(
			classUID,
			[&](dicom::ServiceBase& service, dicom::DataSet& query, dicom::Sequence& matches)
			{
				(void)matches;
				assert(get<std::string>(query, dicom::TAG_QR_LEVEL) == "STUDY");
				handlerStarted = true;

				for(int wait = 0; wait < 20 && !cancelObserved; ++wait)
				{
					const bool observed = dicom::PollCCancelRQ(service);
					if(observed)
						cancelObserved = true;
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CFindSCU findSCU(client, classUID);
				findSCU.writeRQ(query);

				for(int wait = 0; wait < 20 && !handlerStarted; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				findSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				findSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_FIND_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(data.empty());
				completed = cancelObserved;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handlerStarted);
		assert(cancelObserved);
	}

	void checkServerClientCFindFinalCancelStatus()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_FIND_SOP_CLASS;
		std::atomic<bool> handlerStarted(false);
		std::atomic<bool> cancelObserved(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddCancellableFindHandler(
			classUID,
			[&](dicom::ServiceBase& service, dicom::DataSet& query, dicom::Sequence& matches) -> UINT16
			{
				(void)matches;
				assert(get<std::string>(query, dicom::TAG_QR_LEVEL) == "STUDY");
				handlerStarted = true;

				for(int wait = 0; wait < 20 && !cancelObserved; ++wait)
				{
					const bool observed = dicom::PollCCancelRQ(service);
					if(observed)
						cancelObserved = true;
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}

				return cancelObserved ? dicom::Status::CANCEL : dicom::Status::SUCCESS;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CFindSCU findSCU(client, classUID);
				findSCU.writeRQ(query);

				for(int wait = 0; wait < 20 && !handlerStarted; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				findSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				findSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_FIND_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(get<UINT16>(response, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
				assert(data.empty());
				completed = cancelObserved;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handlerStarted);
		assert(cancelObserved);
	}

	void checkServerClientCFindFinalCancelAfterHandlerReturn()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_FIND_SOP_CLASS;
		std::atomic<bool> handlerStarted(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddFindHandler(
			classUID,
			[&](dicom::ServiceBase&, dicom::DataSet& query, dicom::Sequence& matches)
			{
				assert(get<std::string>(query, dicom::TAG_QR_LEVEL) == "STUDY");
				handlerStarted = true;

				dicom::DataSet match;
				match.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				match.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.1553.10"));
				matches.push_back(match);

				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, dicom::UID(""));

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts);
				dicom::CFindSCU findSCU(client, classUID);
				findSCU.writeRQ(query);

				for(int wait = 0; wait < 20 && !handlerStarted; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				findSCU.writeCancelRQ();

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				findSCU.readRSP(status, response, data);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::C_FIND_RSP);
				assert(status == dicom::Status::CANCEL);
				assert(get<UINT16>(response, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
				assert(data.empty());
				completed = true;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(handlerStarted);
	}

	void checkCMove()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.2");
		dicom::CommandSet::CMoveRQ rq(9, classUID, "ARCHIVE_AE");
		assert(get<UINT16>(rq, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RQ);
		assert(get<UINT16>(rq, dicom::TAG_MSG_ID) == 9);
		assert(get<std::string>(rq, dicom::TAG_MOVE_DEST) == "ARCHIVE_AE");
		assert(get<UINT16>(rq, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);

		dicom::CommandSet::CMoveRSP rsp(9, classUID, dicom::Status::SUCCESS, dicom::DataSetStatus::NO_DATA_SET);
		rsp.setCompleted(3);
		assert(get<UINT16>(rsp, dicom::TAG_CMD_FIELD) == dicom::Command::C_MOVE_RSP);
		assert(get<UINT16>(rsp, dicom::TAG_MSG_ID_RSP) == 9);
		assert(get<UINT16>(rsp, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
		assert(get<UINT16>(rsp, dicom::TAG_NUM_COMPL_SUBOP) == 3);
	}
}

int main()
{
#if defined(SIGPIPE)
	signal(SIGPIPE, SIG_IGN);
#endif
	checkCEcho();
	checkCStore();
	checkCFind();
	checkCGet();
	checkCdimseStatusHelpers();
	checkCCancel();
	checkCCancelOverPData();
	checkSCUResponseValidationOverPData();
	checkAssociationNegotiationAndCEcho();
	checkServerClientCEcho();
	checkServerClientCStore();
	checkServerClientCFind();
	checkServerClientCMove();
	checkServerClientCMoveStoreSubOperationScheduler();
	checkServerClientCMoveStoreSubOperationCancel();
	checkServerClientCGet();
	checkServerClientCGetStoreSubOperation();
	checkServerClientCGetStoreSubOperationScheduler();
	checkServerClientCGetFinalCancelStatus();
	checkServerClientCMoveFinalCancelStatus();
	checkServerClientCCancelDispatch();
	checkServerClientCCancelObservedByRunningHandler();
	checkServerClientCFindFinalCancelStatus();
	checkServerClientCFindFinalCancelAfterHandlerReturn();
	checkCMove();
	return 0;
}
