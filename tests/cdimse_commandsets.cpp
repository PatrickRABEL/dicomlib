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

	void checkServerClientCCancelDispatch()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::STUDY_ROOT_QR_FIND_SOP_CLASS;
		std::atomic<bool> findHandled(false);
		std::atomic<bool> cancelHandled(false);

		CancelDispatchLogger logger(cancelHandled);
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
				assert(status == dicom::Status::SUCCESS);
				assert(data.empty());

				for(int wait = 0; wait < 20 && !cancelHandled; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				completed = cancelHandled;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(findHandled);
		assert(cancelHandled);
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
				assert(status == dicom::Status::SUCCESS);
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
	checkCEcho();
	checkCStore();
	checkCFind();
	checkCGet();
	checkCCancel();
	checkCCancelOverPData();
	checkSCUResponseValidationOverPData();
	checkAssociationNegotiationAndCEcho();
	checkServerClientCEcho();
	checkServerClientCStore();
	checkServerClientCFind();
	checkServerClientCMove();
	checkServerClientCGet();
	checkServerClientCCancelDispatch();
	checkServerClientCCancelObservedByRunningHandler();
	checkServerClientCFindFinalCancelStatus();
	checkCMove();
	return 0;
}
