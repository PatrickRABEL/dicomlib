#include "dicomlib/Cdimse.hpp"
#include "dicomlib/aaac.hpp"
#include "dicomlib/ClientConnection.hpp"
#include "dicomlib/CommandSets.hpp"
#include "dicomlib/ImplementationUID.hpp"
#include "dicomlib/Ndimse.hpp"
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

	void makeSocketPair(int sockets[2]);

	dicom::primitive::UserInformation makeUserInformation()
	{
		dicom::primitive::MaximumSubLength maxSubLength;
		maxSubLength.Set(16384);
		dicom::primitive::UserInformation userInfo;
		userInfo.ImpClass_.UID_ = dicom::ImplementationClassUID;
		userInfo.ImpVersion_.Name = dicom::ImplementationVersionName;
		userInfo.SetMax(maxSubLength);
		return userInfo;
	}

	void negotiateAssociation(
		PairedService& scuSide,
		PairedService& scpSide,
		const dicom::UID& classUID)
	{
		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		dicom::primitive::UserInformation userInfo = makeUserInformation();

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

	void checkAssociationExtendedNegotiation()
	{
		int sockets[2];
		makeSocketPair(sockets);
		PairedSocket requestWriter(sockets[0]);
		PairedSocket requestReader(sockets[1]);

		const dicom::UID classUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		dicom::primitive::MaximumSubLength maxSubLength;
		maxSubLength.Set(32768);

		dicom::primitive::UserInformation requestInfo;
		requestInfo.ImpClass_.UID_ = dicom::ImplementationClassUID;
		requestInfo.ImpVersion_.Name = dicom::ImplementationVersionName;
		requestInfo.SetMax(maxSubLength);
		requestInfo.AddSCPSCURoleSelection(classUID,true,true);
		requestInfo.SetAsynchronousOperationsWindow(4,2);
		std::vector<BYTE> requestApplicationInfo;
		requestApplicationInfo.push_back(0x01);
		requestApplicationInfo.push_back(0x02);
		requestApplicationInfo.push_back(0x03);
		requestInfo.AddSOPClassExtendedNegotiation(classUID,requestApplicationInfo);

		dicom::primitive::AAssociateRQ request;
		request.CalledAppTitle_ = "SCP_AE";
		request.CallingAppTitle_ = "SCU_AE";
		request.ProposedPresentationContexts_ = contexts;
		request.SetUserInformation(requestInfo);
		request.Write(requestWriter);

		dicom::primitive::AAssociateRQ readRequest;
		readRequest.Read(requestReader);
		assert(readRequest.UserInfo_.SCPSCURoles_.size() == 1);
		assert(readRequest.UserInfo_.SCPSCURoles_.at(0).UID_ == classUID);
		assert(readRequest.UserInfo_.SCPSCURoles_.at(0).SCURole_ == 1);
		assert(readRequest.UserInfo_.SCPSCURoles_.at(0).SCPRole_ == 1);
		assert(readRequest.UserInfo_.HasAsynchronousOperationsWindow_);
		assert(readRequest.UserInfo_.AsyncOperationsWindow_.MaximumNumberOperationsInvoked_ == 4);
		assert(readRequest.UserInfo_.AsyncOperationsWindow_.MaximumNumberOperationsPerformed_ == 2);
		assert(readRequest.UserInfo_.SOPClassExtendedNegotiations_.size() == 1);
		assert(readRequest.UserInfo_.SOPClassExtendedNegotiations_.at(0).UID_ == classUID);
		assert(readRequest.UserInfo_.SOPClassExtendedNegotiations_.at(0).ServiceClassApplicationInformation_ == requestApplicationInfo);

		int responseSockets[2];
		makeSocketPair(responseSockets);
		PairedSocket responseWriter(responseSockets[0]);
		PairedSocket responseReader(responseSockets[1]);

		dicom::primitive::UserInformation responseInfo;
		responseInfo.ImpClass_.UID_ = dicom::ImplementationClassUID;
		responseInfo.ImpVersion_.Name = dicom::ImplementationVersionName;
		responseInfo.SetMax(maxSubLength);
		responseInfo.AddSCPSCURoleSelection(classUID,true,false);
		responseInfo.SetAsynchronousOperationsWindow(2,1);
		std::vector<BYTE> responseApplicationInfo;
		responseApplicationInfo.push_back(0x05);
		responseInfo.AddSOPClassExtendedNegotiation(classUID,responseApplicationInfo);

		dicom::primitive::PresentationContextAccept accepted =
			acceptFirstContext(contexts, dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		dicom::primitive::AAssociateAC acknowledgement;
		acknowledgement.CalledAppTitle_ = readRequest.CalledAppTitle_;
		acknowledgement.CallingAppTitle_ = readRequest.CallingAppTitle_;
		acknowledgement.AppContext_ = readRequest.AppContext_;
		acknowledgement.PresContextAccepts_.push_back(accepted);
		acknowledgement.SetUserInformation(responseInfo);
		acknowledgement.Write(responseWriter);

		dicom::primitive::AAssociateAC readAcknowledgement;
		readAcknowledgement.Read(responseReader);
		assert(readAcknowledgement.UserInfo_.SCPSCURoles_.size() == 1);
		assert(readAcknowledgement.UserInfo_.SCPSCURoles_.at(0).UID_ == classUID);
		assert(readAcknowledgement.UserInfo_.SCPSCURoles_.at(0).SCURole_ == 1);
		assert(readAcknowledgement.UserInfo_.SCPSCURoles_.at(0).SCPRole_ == 0);
		assert(readAcknowledgement.UserInfo_.HasAsynchronousOperationsWindow_);
		assert(readAcknowledgement.UserInfo_.AsyncOperationsWindow_.MaximumNumberOperationsInvoked_ == 2);
		assert(readAcknowledgement.UserInfo_.AsyncOperationsWindow_.MaximumNumberOperationsPerformed_ == 1);
		assert(readAcknowledgement.UserInfo_.SOPClassExtendedNegotiations_.size() == 1);
		assert(readAcknowledgement.UserInfo_.SOPClassExtendedNegotiations_.at(0).UID_ == classUID);
		assert(readAcknowledgement.UserInfo_.SOPClassExtendedNegotiations_.at(0).ServiceClassApplicationInformation_ == responseApplicationInfo);

		NullService requestorState;
		requestorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		requestorState.ApplyAssociationNegotiationAsRequestor(readAcknowledgement);
		assert(requestorState.CanActAsSCU(classUID));
		assert(!requestorState.CanActAsSCP(classUID));
		assert(requestorState.HasNegotiatedAsynchronousOperationsWindow());
		assert(requestorState.MaximumNumberOperationsInvoked() == 2);
		assert(requestorState.MaximumNumberOperationsPerformed() == 1);
		assert(requestorState.HasNegotiatedSOPClassExtended(classUID));
		assert(requestorState.GetNegotiatedSOPClassExtendedInformation(classUID) == responseApplicationInfo);

		NullService acceptorState;
		acceptorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		acceptorState.AcceptedPresentationContexts_ = readAcknowledgement.PresContextAccepts_;
		acceptorState.ApplyAssociationNegotiationAsAcceptor(readAcknowledgement.UserInfo_);
		assert(!acceptorState.CanActAsSCU(classUID));
		assert(acceptorState.CanActAsSCP(classUID));
		assert(acceptorState.HasNegotiatedAsynchronousOperationsWindow());
		assert(acceptorState.MaximumNumberOperationsInvoked() == 2);
		assert(acceptorState.MaximumNumberOperationsPerformed() == 1);
		assert(acceptorState.HasNegotiatedSOPClassExtended(classUID));
		assert(acceptorState.GetNegotiatedSOPClassExtendedInformation(classUID) == responseApplicationInfo);

		dicom::primitive::AAssociateAC defaultRolesAcknowledgement;
		defaultRolesAcknowledgement.PresContextAccepts_.push_back(accepted);
		NullService defaultRequestorState;
		defaultRequestorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		defaultRequestorState.ApplyAssociationNegotiationAsRequestor(defaultRolesAcknowledgement);
		assert(defaultRequestorState.CanActAsSCU(classUID));
		assert(!defaultRequestorState.CanActAsSCP(classUID));

		NullService defaultAcceptorState;
		defaultAcceptorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		defaultAcceptorState.AcceptedPresentationContexts_ = defaultRolesAcknowledgement.PresContextAccepts_;
		defaultAcceptorState.ApplyAssociationNegotiationAsAcceptor(defaultRolesAcknowledgement.UserInfo_);
		assert(!defaultAcceptorState.CanActAsSCU(classUID));
		assert(defaultAcceptorState.CanActAsSCP(classUID));
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

	struct TestCGetSCU : public dicom::CGetSCU
	{
		TestCGetSCU(dicom::ServiceBase& service, const dicom::UID& classUID)
		: dicom::CGetSCU(service, classUID)
		{
		}

		void setLastMessageID(UINT16 messageID)
		{
			lastMessageID_ = messageID;
		}
	};

	struct TestCMoveSCU : public dicom::CMoveSCU
	{
		TestCMoveSCU(dicom::ServiceBase& service, const dicom::UID& classUID)
		: dicom::CMoveSCU(service, classUID)
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

	void requireRead(dicom::ServiceBase& service, dicom::DataSet& data)
	{
		if(!service.Read(data))
			throw dicom::exception("Unexpected association release while reading test P-DATA");
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

	void checkNdimseCommandSets()
	{
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.20");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.20.1");
		const UINT16 eventTypeID = 3;
		const UINT16 actionTypeID = 5;
		const UINT16 invalidDataSetType = 0xffff;

		dicom::CommandSet::NEventReportRQ eventRQ(
			21,
			classUID,
			instUID,
			eventTypeID,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<dicom::UID>(eventRQ, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(eventRQ, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(eventRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RQ);
		assert(get<UINT16>(eventRQ, dicom::TAG_MSG_ID) == 21);
		assert(get<UINT16>(eventRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(eventRQ, dicom::TAG_EVENT_TYPE_ID) == eventTypeID);

		dicom::CommandSet::NEventReportRSP eventRSP(
			21,
			classUID,
			instUID,
			dicom::Status::SUCCESS,
			eventTypeID,
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<dicom::UID>(eventRSP, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(eventRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(eventRSP, dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
		assert(get<UINT16>(eventRSP, dicom::TAG_MSG_ID_RSP) == 21);
		assert(get<UINT16>(eventRSP, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
		assert(get<UINT16>(eventRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(eventRSP, dicom::TAG_EVENT_TYPE_ID) == eventTypeID);

		std::vector<dicom::Tag> attrList;
		attrList.push_back(dicom::TAG_PAT_NAME);
		attrList.push_back(dicom::TAG_PAT_ID);
		dicom::CommandSet::NGetRQ getRQ(23,classUID,instUID,attrList);
		assert(get<dicom::UID>(getRQ, dicom::TAG_REQ_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(getRQ, dicom::TAG_REQ_SOP_INST_UID) == instUID);
		assert(get<UINT16>(getRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RQ);
		assert(get<UINT16>(getRQ, dicom::TAG_MSG_ID) == 23);
		assert(get<UINT16>(getRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		std::vector<dicom::Value> attrValues = getRQ.Values(dicom::TAG_ATTR_ID_LIST);
		assert(attrValues.size() == 2);
		dicom::Tag attrTag = dicom::TAG_NULL;
		attrValues.at(0) >> attrTag;
		assert(attrTag == dicom::TAG_PAT_NAME);
		attrValues.at(1) >> attrTag;
		assert(attrTag == dicom::TAG_PAT_ID);
		assert(!getRQ.exists(dicom::TAG_MSG_ID_RSP));

		dicom::CommandSet::NGetRSP getRSP(
			23,
			classUID,
			instUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<dicom::UID>(getRSP, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(getRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(getRSP, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
		assert(get<UINT16>(getRSP, dicom::TAG_MSG_ID_RSP) == 23);
		assert(get<UINT16>(getRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(getRSP, dicom::TAG_STATUS) == dicom::Status::SUCCESS);

		dicom::CommandSet::NSetRQ setRQ(25,classUID,instUID);
		assert(get<dicom::UID>(setRQ, dicom::TAG_REQ_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(setRQ, dicom::TAG_REQ_SOP_INST_UID) == instUID);
		assert(get<UINT16>(setRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RQ);
		assert(get<UINT16>(setRQ, dicom::TAG_MSG_ID) == 25);
		assert(get<UINT16>(setRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(!setRQ.exists(dicom::TAG_MSG_ID_RSP));

		dicom::CommandSet::NSetRSP setRSP(
			25,
			classUID,
			instUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<dicom::UID>(setRSP, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(setRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(setRSP, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
		assert(get<UINT16>(setRSP, dicom::TAG_MSG_ID_RSP) == 25);
		assert(get<UINT16>(setRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(setRSP, dicom::TAG_STATUS) == dicom::Status::SUCCESS);

		dicom::CommandSet::NActionRQ actionRQ(
			27,
			classUID,
			instUID,
			actionTypeID,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<dicom::UID>(actionRQ, dicom::TAG_REQ_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(actionRQ, dicom::TAG_REQ_SOP_INST_UID) == instUID);
		assert(get<UINT16>(actionRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RQ);
		assert(get<UINT16>(actionRQ, dicom::TAG_MSG_ID) == 27);
		assert(get<UINT16>(actionRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(actionRQ, dicom::TAG_ACTION_TYPE_ID) == actionTypeID);
		assert(!actionRQ.exists(dicom::TAG_MSG_ID_RSP));

		dicom::CommandSet::NActionRSP actionRSP(
			27,
			classUID,
			instUID,
			dicom::Status::SUCCESS,
			actionTypeID,
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<dicom::UID>(actionRSP, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(actionRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(actionRSP, dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
		assert(get<UINT16>(actionRSP, dicom::TAG_MSG_ID_RSP) == 27);
		assert(get<UINT16>(actionRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(actionRSP, dicom::TAG_STATUS) == dicom::Status::SUCCESS);
		assert(get<UINT16>(actionRSP, dicom::TAG_ACTION_TYPE_ID) == actionTypeID);

		dicom::CommandSet::NCreateRQ createRQ(
			29,
			classUID,
			instUID,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<dicom::UID>(createRQ, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(createRQ, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(createRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RQ);
		assert(get<UINT16>(createRQ, dicom::TAG_MSG_ID) == 29);
		assert(get<UINT16>(createRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(!createRQ.exists(dicom::TAG_MSG_ID_RSP));

		dicom::CommandSet::NCreateRSP createRSP(
			29,
			classUID,
			instUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<dicom::UID>(createRSP, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(createRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(createRSP, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
		assert(get<UINT16>(createRSP, dicom::TAG_MSG_ID_RSP) == 29);
		assert(get<UINT16>(createRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(createRSP, dicom::TAG_STATUS) == dicom::Status::SUCCESS);

		dicom::CommandSet::NDeleteRQ deleteRQ(31,classUID,instUID);
		assert(get<dicom::UID>(deleteRQ, dicom::TAG_REQ_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(deleteRQ, dicom::TAG_REQ_SOP_INST_UID) == instUID);
		assert(get<UINT16>(deleteRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_DELETE_RQ);
		assert(get<UINT16>(deleteRQ, dicom::TAG_MSG_ID) == 31);
		assert(get<UINT16>(deleteRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		assert(!deleteRQ.exists(dicom::TAG_MSG_ID_RSP));

		dicom::CommandSet::NDeleteRSP deleteRSP(31,classUID,instUID,dicom::Status::SUCCESS);
		assert(get<dicom::UID>(deleteRSP, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(deleteRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(deleteRSP, dicom::TAG_CMD_FIELD) == dicom::Command::N_DELETE_RSP);
		assert(get<UINT16>(deleteRSP, dicom::TAG_MSG_ID_RSP) == 31);
		assert(get<UINT16>(deleteRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(deleteRSP, dicom::TAG_STATUS) == dicom::Status::SUCCESS);

		dicom::CommandSet::NActionRQ nonNullDataSetTypeRQ(
			33,
			classUID,
			instUID,
			actionTypeID,
			invalidDataSetType);
		assert(get<UINT16>(nonNullDataSetTypeRQ, dicom::TAG_DATA_SET_TYPE) == invalidDataSetType);
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

	void checkNdimseStatusHelpers()
	{
		assert(dicom::IsNdimseSuccessStatus(dicom::Status::SUCCESS));
		assert(!dicom::IsNdimseSuccessStatus(0x0001));

		assert(dicom::IsNdimseWarningStatus(0x0001));
		assert(dicom::IsNdimseWarningStatus(0x0107));
		assert(dicom::IsNdimseWarningStatus(0x0116));
		assert(dicom::IsNdimseWarningStatus(0xb000));
		assert(dicom::IsNdimseWarningStatus(0xb123));
		assert(!dicom::IsNdimseWarningStatus(0x0110));

		assert(dicom::IsNdimseFailureStatus(0x0101));
		assert(dicom::IsNdimseFailureStatus(0x0110));
		assert(dicom::IsNdimseFailureStatus(0x0122));
		assert(dicom::IsNdimseFailureStatus(0x0210));
		assert(dicom::IsNdimseFailureStatus(0x0213));
		assert(dicom::IsNdimseFailureStatus(0xa001));
		assert(dicom::IsNdimseFailureStatus(0xc123));
		assert(!dicom::IsNdimseFailureStatus(0x0107));
		assert(!dicom::IsNdimseFailureStatus(0x0116));
		assert(!dicom::IsNdimseFailureStatus(dicom::Status::SUCCESS));

		assert(dicom::IsNdimseFinalStatus(dicom::Status::SUCCESS));
		assert(dicom::IsNdimseFinalStatus(0x0001));
		assert(dicom::IsNdimseFinalStatus(0x0110));
		assert(!dicom::IsNdimseFinalStatus(dicom::Status::CANCEL));
		assert(!dicom::IsNdimseFinalStatus(dicom::Status::PENDING));
		assert(!dicom::IsNdimseFinalStatus(dicom::Status::PENDING1));

		assert(dicom::IsNEventReportResponseStatus(0x0000));
		assert(dicom::IsNGetResponseStatus(0x0107));
		assert(dicom::IsNSetResponseStatus(0x0116));
		assert(dicom::IsNActionResponseStatus(0x0212));
		assert(dicom::IsNCreateResponseStatus(0xb000));
		assert(dicom::IsNDeleteResponseStatus(0xc123));
		assert(!dicom::IsNEventReportResponseStatus(dicom::Status::CANCEL));
		assert(!dicom::IsNGetResponseStatus(dicom::Status::PENDING));
	}

	void checkNdimseSCUOverPData()
	{
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.21");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.21.1");

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NEventReportSCU scu(scuService,classUID);
			scu.writeRQ(instUID,3);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RQ);
			assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_CLASS_UID) == classUID);
			assert(get<dicom::UID>(command, dicom::TAG_AFF_SOP_INST_UID) == instUID);
			assert(get<UINT16>(command, dicom::TAG_EVENT_TYPE_ID) == 3);
			assert(get<UINT16>(command, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);

			dicom::CommandSet::NEventReportRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				3,
				dicom::DataSetStatus::NO_DATA_SET);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			std::vector<dicom::Tag> attrList;
			attrList.push_back(dicom::TAG_PAT_NAME);
			attrList.push_back(dicom::TAG_PAT_ID);
			dicom::NGetSCU scu(scuService,classUID);
			scu.writeRQ(instUID,attrList);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RQ);
			assert(get<dicom::UID>(command, dicom::TAG_REQ_SOP_CLASS_UID) == classUID);
			assert(get<dicom::UID>(command, dicom::TAG_REQ_SOP_INST_UID) == instUID);
			assert(command.Values(dicom::TAG_ATTR_ID_LIST).size() == 2);

			dicom::CommandSet::NGetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::YES_DATA_SET);
			scpService.WriteCommand(responseCommand,classUID);
			dicom::DataSet responseData;
			responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, classUID);
			responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
			scpService.WriteDataSet(responseData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == instUID);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::DataSet requestData;
			requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
			dicom::NSetSCU scu(scuService,classUID);
			scu.writeRQ(instUID,requestData);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RQ);
			dicom::DataSet readRequestData;
			requireRead(scpService,readRequestData);
			assert(get<dicom::UID>(readRequestData, dicom::TAG_SOP_INST_UID) == instUID);

			dicom::CommandSet::NSetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NActionSCU scu(scuService,classUID);
			scu.writeRQ(instUID,7);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RQ);
			assert(get<UINT16>(command, dicom::TAG_ACTION_TYPE_ID) == 7);

			dicom::CommandSet::NActionRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				7,
				dicom::DataSetStatus::NO_DATA_SET);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::DataSet requestData;
			requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
			dicom::NCreateSCU scu(scuService,classUID);
			scu.writeRQ(instUID,requestData);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RQ);
			dicom::DataSet readRequestData;
			requireRead(scpService,readRequestData);
			assert(get<dicom::UID>(readRequestData, dicom::TAG_SOP_INST_UID) == instUID);

			dicom::CommandSet::NCreateRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NDeleteSCU scu(scuService,classUID);
			scu.writeRQ(instUID);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			assert(get<UINT16>(command, dicom::TAG_CMD_FIELD) == dicom::Command::N_DELETE_RQ);
			assert(get<dicom::UID>(command, dicom::TAG_REQ_SOP_INST_UID) == instUID);

			dicom::CommandSet::NDeleteRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_DELETE_RSP);
			assert(data.empty());
		}
	}

	void checkNdimseSCUValidation()
	{
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.22");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.22.1");

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
		dicom::primitive::PresentationContextAccept accepted =
			acceptFirstContext(contexts, dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		dicom::primitive::AAssociateAC acknowledgement;
		acknowledgement.PresContextAccepts_.push_back(accepted);
		acknowledgement.UserInfo_ = makeUserInformation();
		acknowledgement.UserInfo_.AddSCPSCURoleSelection(classUID,false,true);

		NullService requestorState;
		requestorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		requestorState.ApplyAssociationNegotiationAsRequestor(acknowledgement);

		bool roleRejected = false;
		try
		{
			dicom::NDeleteSCU scu(requestorState,classUID);
			scu.writeRQ(instUID);
		}
		catch(const dicom::exception&)
		{
			roleRejected = true;
		}
		if(!roleRejected)
			throw dicom::exception("N-DIMSE SCU role selection was not enforced");

		int sockets[2];
		makeSocketPair(sockets);
		PairedService scuService(sockets[0], classUID);
		PairedService scpService(sockets[1], classUID);

		dicom::NDeleteSCU scu(scuService,classUID);
		scu.writeRQ(instUID);

		dicom::DataSet command;
		requireRead(scpService,command);
		const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
		dicom::CommandSet::NDeleteRSP responseCommand(
			messageID,
			classUID,
			instUID,
			dicom::Status::PENDING);
		scpService.WriteCommand(responseCommand,classUID);

		bool statusRejected = false;
		try
		{
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
		}
		catch(const dicom::exception&)
		{
			statusRejected = true;
		}
		if(!statusRejected)
			throw dicom::exception("N-DIMSE non-final response status was not rejected");

		{
			int classMismatchSockets[2];
			makeSocketPair(classMismatchSockets);
			PairedService classMismatchSCUService(classMismatchSockets[0], classUID);
			PairedService classMismatchSCPService(classMismatchSockets[1], classUID);
			const dicom::UID wrongClassUID("1.2.826.0.1.3680043.10.1553.22.99");

			dicom::NDeleteSCU classMismatchSCU(classMismatchSCUService,classUID);
			classMismatchSCU.writeRQ(instUID);

			dicom::DataSet classMismatchRequest;
			requireRead(classMismatchSCPService,classMismatchRequest);
			const UINT16 classMismatchMessageID = get<UINT16>(classMismatchRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NDeleteRSP classMismatchResponse(
				classMismatchMessageID,
				wrongClassUID,
				instUID,
				dicom::Status::SUCCESS);
			classMismatchSCPService.WriteCommand(classMismatchResponse,classUID);

			bool classMismatchRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				classMismatchSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				classMismatchRejected = true;
			}
			if(!classMismatchRejected)
				throw dicom::exception("N-DIMSE response SOP Class UID mismatch was not rejected");
		}

		dicom::primitive::AAssociateAC scpDeniedAcknowledgement;
		scpDeniedAcknowledgement.PresContextAccepts_.push_back(accepted);
		scpDeniedAcknowledgement.UserInfo_ = makeUserInformation();
		scpDeniedAcknowledgement.UserInfo_.AddSCPSCURoleSelection(classUID,false,true);

		NullService acceptorState;
		acceptorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		acceptorState.AcceptedPresentationContexts_ = scpDeniedAcknowledgement.PresContextAccepts_;
		acceptorState.ApplyAssociationNegotiationAsAcceptor(scpDeniedAcknowledgement.UserInfo_);

		dicom::CommandSet::NDeleteRQ deniedRequest(3,classUID,instUID);
		bool scpRoleRejected = false;
		try
		{
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				acceptorState,
				deniedRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			scpRoleRejected = true;
		}
		if(!scpRoleRejected)
			throw dicom::exception("N-DIMSE SCP role selection was not enforced");

		NullService invalidStatusService;
		invalidStatusService.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		invalidStatusService.AcceptedPresentationContexts_.push_back(accepted);
		bool scpStatusRejected = false;
		try
		{
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::PENDING;
				},
				invalidStatusService,
				deniedRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			scpStatusRejected = true;
		}
		if(!scpStatusRejected)
			throw dicom::exception("N-DIMSE SCP invalid response status was not rejected");

		dicom::DataSet invalidNGetRequest;
		invalidNGetRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		invalidNGetRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_GET_RQ);
		invalidNGetRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(5));
		invalidNGetRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		invalidNGetRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID, instUID);
		bool nGetDataSetRejected = false;
		try
		{
			dicom::HandleNGet(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				invalidNGetRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			nGetDataSetRejected = true;
		}
		if(!nGetDataSetRejected)
			throw dicom::exception("N-GET request data set was not rejected");

		dicom::DataSet invalidNSetRequest;
		invalidNSetRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		invalidNSetRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_SET_RQ);
		invalidNSetRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(7));
		invalidNSetRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		invalidNSetRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID, instUID);
		bool nSetMissingDataSetRejected = false;
		try
		{
			dicom::HandleNSet(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				invalidNSetRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			nSetMissingDataSetRejected = true;
		}
		if(!nSetMissingDataSetRejected)
			throw dicom::exception("N-SET missing request data set was not rejected");

		dicom::DataSet invalidNDeleteRequest;
		invalidNDeleteRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		invalidNDeleteRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_DELETE_RQ);
		invalidNDeleteRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(9));
		invalidNDeleteRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		invalidNDeleteRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID, instUID);
		bool nDeleteDataSetRejected = false;
		try
		{
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				invalidNDeleteRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			nDeleteDataSetRejected = true;
		}
		if(!nDeleteDataSetRejected)
			throw dicom::exception("N-DELETE request data set was not rejected");

		bool nDeleteResponseDataRejected = false;
		try
		{
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.1553.22.2"));
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				deniedRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			nDeleteResponseDataRejected = true;
		}
		if(!nDeleteResponseDataRejected)
			throw dicom::exception("N-DELETE response data set was not rejected");

		dicom::CommandSet::NGetRQ wrongCommand(11,classUID,instUID,std::vector<dicom::Tag>());
		bool wrongCommandRejected = false;
		try
		{
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				wrongCommand,
				classUID);
		}
		catch(const dicom::exception&)
		{
			wrongCommandRejected = true;
		}
		if(!wrongCommandRejected)
			throw dicom::exception("N-DIMSE wrong request command field was not rejected");

		const dicom::UID wrongClassUID("1.2.826.0.1.3680043.10.1553.22.98");
		dicom::CommandSet::NDeleteRQ wrongClassRequest(13,wrongClassUID,instUID);
		bool wrongClassRejected = false;
		try
		{
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				wrongClassRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			wrongClassRejected = true;
		}
		if(!wrongClassRejected)
			throw dicom::exception("N-DIMSE request SOP Class UID mismatch was not rejected");
	}

	void checkNdimseSCPOverPData()
	{
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.23");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.23.1");

		dicom::NHandlerFunction successHandler =
			[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
			{
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.1553.23.99"));
				return dicom::Status::SUCCESS;
			};

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NEventReportSCU scu(scuService,classUID);
			scu.writeRQ(instUID,3);

			dicom::DataSet command;
			requireRead(scpService,command);
			dicom::HandleNEventReport(successHandler,scpService,command,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
			assert(get<UINT16>(response, dicom::TAG_EVENT_TYPE_ID) == 3);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == dicom::UID("1.2.826.0.1.3680043.10.1553.23.99"));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			std::vector<dicom::Tag> attrList;
			attrList.push_back(dicom::TAG_PAT_NAME);
			attrList.push_back(dicom::TAG_PAT_ID);
			dicom::NGetSCU scu(scuService,classUID);
			scu.writeRQ(instUID,attrList);

			dicom::DataSet command;
			requireRead(scpService,command);
			assert(command.Values(dicom::TAG_ATTR_ID_LIST).size() == 2);
			dicom::HandleNGet(successHandler,scpService,command,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == dicom::UID("1.2.826.0.1.3680043.10.1553.23.99"));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::DataSet requestData;
			requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
			dicom::NSetSCU scu(scuService,classUID);
			scu.writeRQ(instUID,requestData);

			dicom::DataSet command;
			requireRead(scpService,command);
			dicom::HandleNSet(successHandler,scpService,command,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == dicom::UID("1.2.826.0.1.3680043.10.1553.23.99"));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NActionSCU scu(scuService,classUID);
			scu.writeRQ(instUID,7);

			dicom::DataSet command;
			requireRead(scpService,command);
			dicom::HandleNAction(successHandler,scpService,command,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
			assert(get<UINT16>(response, dicom::TAG_ACTION_TYPE_ID) == 7);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == dicom::UID("1.2.826.0.1.3680043.10.1553.23.99"));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::DataSet requestData;
			requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
			dicom::NCreateSCU scu(scuService,classUID);
			scu.writeRQ(instUID,requestData);

			dicom::DataSet command;
			requireRead(scpService,command);
			dicom::HandleNCreate(successHandler,scpService,command,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == dicom::UID("1.2.826.0.1.3680043.10.1553.23.99"));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NDeleteSCU scu(scuService,classUID);
			scu.writeRQ(instUID);

			dicom::DataSet command;
			requireRead(scpService,command);
			dicom::HandleNDelete(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				scpService,
				command,
				classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_DELETE_RSP);
			assert(data.empty());
		}
	}

	void checkCdimseRoleEnforcement()
	{
		const dicom::UID classUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.11.1");

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
		dicom::primitive::PresentationContextAccept accepted =
			acceptFirstContext(contexts, dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		dicom::primitive::AAssociateAC acknowledgement;
		acknowledgement.PresContextAccepts_.push_back(accepted);
		acknowledgement.UserInfo_ = makeUserInformation();
		acknowledgement.UserInfo_.AddSCPSCURoleSelection(classUID,false,true);

		NullService requestorState;
		requestorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		requestorState.ApplyAssociationNegotiationAsRequestor(acknowledgement);
		assert(!requestorState.CanActAsSCU(classUID));
		assert(requestorState.CanActAsSCP(classUID));

		dicom::DataSet stored;
		stored.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, classUID);
		stored.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instanceUID);

		bool scuRejected = false;
		try
		{
			dicom::CStoreSCU storeSCU(requestorState,classUID);
			storeSCU.writeRQ(instanceUID,stored);
		}
		catch(const dicom::exception&)
		{
			scuRejected = true;
		}
		assert(scuRejected);

		NullService acceptorState;
		acceptorState.AAssociateRQ_.ProposedPresentationContexts_ = contexts;
		acceptorState.AcceptedPresentationContexts_ = acknowledgement.PresContextAccepts_;
		acceptorState.ApplyAssociationNegotiationAsAcceptor(acknowledgement.UserInfo_);
		assert(acceptorState.CanActAsSCU(classUID));
		assert(!acceptorState.CanActAsSCP(classUID));

		dicom::CommandSet::CStoreRQ request(1,classUID,instanceUID);
		bool scpRejected = false;
		try
		{
			dicom::HandleCStore(
				[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
				{
				},
				acceptorState,
				request,
				classUID);
		}
		catch(const dicom::exception&)
		{
			scpRejected = true;
		}
		assert(scpRejected);
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
		const dicom::UID wrongClassUID("1.2.840.10008.5.1.4.1.2.1.1");

		{
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

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(17);

			dicom::CommandSet::CFindRSP wrongClass(
				17,
				wrongClassUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(wrongClass, classUID);

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

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(19);

			dicom::CommandSet::CFindRSP invalidStatus(
				19,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(invalidStatus, classUID);

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

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], dicom::VERIFICATION_SOP_CLASS);
			PairedService scpSide(sockets[1], dicom::VERIFICATION_SOP_CLASS);

			dicom::CEchoSCU scu(scuSide);
			scu.writeRQ();

			dicom::DataSet request;
			requireRead(scpSide, request);
			const UINT16 messageID = get<UINT16>(request, dicom::TAG_MSG_ID);
			dicom::DataSet responseCommand;
			responseCommand.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,dicom::VERIFICATION_SOP_CLASS);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::C_ECHO_RSP);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,messageID);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			scpSide.WriteCommand(responseCommand,dicom::VERIFICATION_SOP_CLASS);

			UINT16 status = 0;
			dicom::DataSet response;
			bool rejected = false;
			try
			{
				scu.readRSP(status,response);
			}
			catch(const std::exception&)
			{
				rejected = true;
			}
			assert(rejected);
		}

		{
			const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.12.1");
			const dicom::UID wrongInstanceUID("1.2.826.0.1.3680043.10.1553.12.2");
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			PairedService scpSide(sockets[1], dicom::CT_IMAGE_STORAGE_SOP_CLASS);

			dicom::DataSet stored;
			stored.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			stored.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instanceUID);

			dicom::CStoreSCU scu(scuSide, dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			scu.writeRQ(instanceUID, stored);

			dicom::DataSet request;
			requireRead(scpSide, request);
			const UINT16 messageID = get<UINT16>(request, dicom::TAG_MSG_ID);
			dicom::CommandSet::CStoreRSP wrongInstance(
				messageID,
				dicom::CT_IMAGE_STORAGE_SOP_CLASS,
				wrongInstanceUID,
				dicom::Status::SUCCESS);
			scpSide.WriteCommand(wrongInstance, dicom::CT_IMAGE_STORAGE_SOP_CLASS);

			UINT16 status = 0;
			dicom::DataSet response;
			bool rejected = false;
			try
			{
				scu.readRSP(status, response);
			}
			catch(const std::exception&)
			{
				rejected = true;
			}
			assert(rejected);
		}

		{
			const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.12.3");
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			PairedService scpSide(sockets[1], dicom::CT_IMAGE_STORAGE_SOP_CLASS);

			dicom::DataSet stored;
			stored.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			stored.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instanceUID);

			dicom::CStoreSCU scu(scuSide, dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			scu.writeRQ(instanceUID, stored);

			dicom::DataSet request;
			requireRead(scpSide, request);
			const UINT16 messageID = get<UINT16>(request, dicom::TAG_MSG_ID);
			dicom::DataSet responseCommand;
			responseCommand.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			responseCommand.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instanceUID);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::C_STORE_RSP);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,messageID);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			scpSide.WriteCommand(responseCommand, dicom::CT_IMAGE_STORAGE_SOP_CLASS);

			UINT16 status = 0;
			dicom::DataSet response;
			bool rejected = false;
			try
			{
				scu.readRSP(status, response);
			}
			catch(const std::exception&)
			{
				rejected = true;
			}
			assert(rejected);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			{
				PairedService scpSide(sockets[1], classUID);
			}

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(27);

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

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			{
				PairedService scpSide(sockets[1], classUID);
				dicom::CommandSet::CFindRSP pendingWithoutData(
					29,
					classUID,
					dicom::Status::PENDING,
					dicom::DataSetStatus::YES_DATA_SET);
				scpSide.WriteCommand(pendingWithoutData, classUID);
			}

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(29);

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
	}

	void checkCdimseSCPRequestValidation()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.1");
		const dicom::UID wrongClassUID("1.2.840.10008.5.1.4.1.2.1.1");
		NullService service;

		dicom::CommandSet::CMoveRQ moveRequest(21,classUID,"ARCHIVE_AE");
		bool wrongCommandRejected = false;
		try
		{
			dicom::HandleCFind(
				dicom::CFindStatusFunction(
					[](dicom::ServiceBase&, dicom::DataSet&, dicom::Sequence&)
					{
						return dicom::Status::SUCCESS;
					}),
				service,
				moveRequest,
				classUID);
		}
		catch(const std::exception&)
		{
			wrongCommandRejected = true;
		}
		assert(wrongCommandRejected);

		dicom::CommandSet::CEchoRQ wrongClassEcho(23,wrongClassUID);
		bool wrongClassRejected = false;
		try
		{
			dicom::HandleCEcho(service, wrongClassEcho, dicom::VERIFICATION_SOP_CLASS);
		}
		catch(const std::exception&)
		{
			wrongClassRejected = true;
		}
		assert(wrongClassRejected);

		dicom::DataSet echoWithDataSet;
		echoWithDataSet.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,dicom::VERIFICATION_SOP_CLASS);
		echoWithDataSet.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::C_ECHO_RQ);
		echoWithDataSet.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(24));
		echoWithDataSet.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
		bool echoDataSetRejected = false;
		try
		{
			dicom::HandleCEcho(service, echoWithDataSet, dicom::VERIFICATION_SOP_CLASS);
		}
		catch(const std::exception&)
		{
			echoDataSetRejected = true;
		}
		assert(echoDataSetRejected);

		dicom::CommandSet::CFindRQ notCancel(25,classUID);
		bool wrongCancelCommandRejected = false;
		try
		{
			dicom::HandleCCancel(service, notCancel);
		}
		catch(const std::exception&)
		{
			wrongCancelCommandRejected = true;
		}
		assert(wrongCancelCommandRejected);

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scpSide(sockets[1], classUID);
			{
				PairedService requestorSide(sockets[0], classUID);
			}

			dicom::CommandSet::CFindRQ request(27,classUID);
			bool missingDataRejected = false;
			try
			{
				dicom::HandleCFind(
					dicom::CFindStatusFunction(
						[](dicom::ServiceBase&, dicom::DataSet&, dicom::Sequence&)
						{
							return dicom::Status::SUCCESS;
						}),
					scpSide,
					request,
					classUID);
			}
			catch(const std::exception&)
			{
				missingDataRejected = true;
			}
			assert(missingDataRejected);
		}

		{
			const dicom::UID storageClassUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
			const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.13.1");
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scpSide(sockets[1], storageClassUID);
			{
				PairedService requestorSide(sockets[0], storageClassUID);
			}

			dicom::CommandSet::CStoreRQ request(29,storageClassUID,instanceUID);
			bool missingDataRejected = false;
			try
			{
				dicom::HandleCStore(
					[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
					{
					},
					scpSide,
					request,
					storageClassUID);
			}
			catch(const std::exception&)
			{
				missingDataRejected = true;
			}
			assert(missingDataRejected);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CFindRQ request(31,classUID);
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			bool invalidStatusRejected = false;
			try
			{
				dicom::HandleCFind(
					dicom::CFindStatusFunction(
						[](dicom::ServiceBase&, dicom::DataSet&, dicom::Sequence&)
						{
							return dicom::Status::PENDING;
						}),
					scpSide,
					readRequest,
					classUID);
			}
			catch(const std::exception&)
			{
				invalidStatusRejected = true;
			}
			assert(invalidStatusRejected);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CGetRQ request(39,classUID);
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			dicom::HandleCGet(
				dicom::CGetStatusFunction(
					[](dicom::ServiceBase& service, const dicom::DataSet&, dicom::DataSet&)
					{
						service.RequestCancel(39);
						return dicom::CSubOperationResult(dicom::Status::PENDING,2,1,0,0);
					}),
				scpSide,
				readRequest,
				classUID);

			TestCGetSCU scu(requestorSide,classUID);
			scu.setLastMessageID(39);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::CANCEL);
			assert(get<UINT16>(response,dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CFindRQ request(37,classUID);
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			dicom::HandleCFind(
				dicom::CFindStatusFunction(
					[](dicom::ServiceBase& service, dicom::DataSet&, dicom::Sequence&)
					{
						service.RequestCancel(37);
						return dicom::Status::PENDING;
					}),
				scpSide,
				readRequest,
				classUID);

			TestCFindSCU scu(requestorSide,classUID);
			scu.setLastMessageID(37);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::CANCEL);
			assert(get<UINT16>(response,dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CGetRQ request(33,classUID);
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			bool invalidStatusRejected = false;
			try
			{
				dicom::HandleCGet(
					dicom::CGetStatusFunction(
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
							return dicom::CSubOperationResult(dicom::Status::PENDING,0,0,0,0);
						}),
					scpSide,
					readRequest,
					classUID);
			}
			catch(const std::exception&)
			{
				invalidStatusRejected = true;
			}
			assert(invalidStatusRejected);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CMoveRQ request(35,classUID,"ARCHIVE_AE");
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			bool invalidStatusRejected = false;
			try
			{
				dicom::HandleCMove(
					dicom::CMoveStatusFunction(
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
							return dicom::CSubOperationResult(dicom::Status::PENDING,0,0,0,0);
						}),
					scpSide,
					readRequest,
					classUID);
			}
			catch(const std::exception&)
			{
				invalidStatusRejected = true;
			}
			assert(invalidStatusRejected);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CMoveRQ request(41,classUID,"ARCHIVE_AE");
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			dicom::HandleCMove(
				dicom::CMoveStatusFunction(
					[](dicom::ServiceBase& service, const dicom::DataSet&, dicom::DataSet&)
					{
						service.RequestCancel(41);
						return dicom::CSubOperationResult(dicom::Status::PENDING,2,1,0,0);
					}),
				scpSide,
				readRequest,
				classUID);

			TestCMoveSCU scu(requestorSide,classUID);
			scu.setLastMessageID(41);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::CANCEL);
			assert(get<UINT16>(response,dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
			assert(data.empty());
		}
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

	void checkServerClientNdimseDispatch()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.24");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.24.1");
		const dicom::UID responseUID("1.2.826.0.1.3680043.10.1553.24.99");
		std::atomic<bool> nGetAttributeListHandled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);

		dicom::NHandlerFunction handler =
			[classUID,responseUID](
				dicom::ServiceBase& service,
				const dicom::DataSet&,
				const dicom::DataSet&,
				dicom::DataSet& responseData)
			{
				assert(service.HasNegotiatedRole(classUID));
				assert(service.CanActAsSCP(classUID));
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, responseUID);
				return dicom::Status::SUCCESS;
			};
		server.AddNEventReportHandler(classUID,handler);
		server.AddNGetHandler(
			classUID,
			[classUID,responseUID,&nGetAttributeListHandled](
				dicom::ServiceBase& service,
				const dicom::DataSet& command,
				const dicom::DataSet&,
				dicom::DataSet& responseData)
			{
				assert(service.HasNegotiatedRole(classUID));
				assert(service.CanActAsSCP(classUID));
				assert(command.Values(dicom::TAG_ATTR_ID_LIST).size() == 2);
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, responseUID);
				nGetAttributeListHandled = true;
				return dicom::Status::SUCCESS;
			});
		server.AddNSetHandler(classUID,handler);
		server.AddNActionHandler(classUID,handler);
		server.AddNCreateHandler(classUID,handler);
		server.AddNDeleteHandler(
			classUID,
			[classUID](dicom::ServiceBase& service, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
			{
				assert(service.HasNegotiatedRole(classUID));
				assert(service.CanActAsSCP(classUID));
				return dicom::Status::SUCCESS;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
		dicom::primitive::UserInformation userInfo = makeUserInformation();
		userInfo.AddSCPSCURoleSelection(classUID,true,false);

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts, userInfo);
				assert(client.HasNegotiatedRole(classUID));
				assert(client.CanActAsSCU(classUID));
				assert(!client.CanActAsSCP(classUID));

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;

				dicom::NEventReportSCU eventSCU(client,classUID);
				eventSCU.writeRQ(instUID,3);
				eventSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == responseUID);

				std::vector<dicom::Tag> attrList;
				attrList.push_back(dicom::TAG_PAT_NAME);
				attrList.push_back(dicom::TAG_PAT_ID);
				response.clear();
				data.clear();
				dicom::NGetSCU getSCU(client,classUID);
				getSCU.writeRQ(instUID,attrList);
				getSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == responseUID);

				dicom::DataSet requestData;
				requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
				response.clear();
				data.clear();
				dicom::NSetSCU setSCU(client,classUID);
				setSCU.writeRQ(instUID,requestData);
				setSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == responseUID);

				response.clear();
				data.clear();
				dicom::NActionSCU actionSCU(client,classUID);
				actionSCU.writeRQ(instUID,7);
				actionSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == responseUID);

				response.clear();
				data.clear();
				dicom::NCreateSCU createSCU(client,classUID);
				createSCU.writeRQ(instUID,requestData);
				createSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == responseUID);

				response.clear();
				data.clear();
				dicom::NDeleteSCU deleteSCU(client,classUID);
				deleteSCU.writeRQ(instUID);
				deleteSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_DELETE_RSP);
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
		assert(nGetAttributeListHandled);
	}

	void checkServerClientExtendedNegotiationState()
	{
		const short port = reserveLocalPort();
		const dicom::UID classUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		std::atomic<bool> extendedNegotiationHandled(false);

		QuietLogger logger;
		dicom::Server server;
		server.SetLogger(&logger);
		server.SetCheckLocalAETCallback(acceptAnyLocalAET);
		server.SetCheckRemoteAETCallback(acceptAnyRemoteAET);
		server.AddHandler(
			classUID,
			[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
			{
			});
		server.SetSOPClassExtendedNegotiationCallback(
			[&](const dicom::UID& uid, const std::vector<BYTE>& request, std::vector<BYTE>& response)
			{
				assert(uid == classUID);
				assert(request.size() == 1);
				assert(request.at(0) == 0x01);
				response.push_back(0x09);
				extendedNegotiationHandled = true;
				return true;
			});
		server.ServeInNewThread(port);

		dicom::PresentationContexts contexts;
		contexts.Add(classUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		dicom::primitive::MaximumSubLength maxSubLength;
		maxSubLength.Set(32768);
		dicom::primitive::UserInformation userInfo;
		userInfo.ImpClass_.UID_ = dicom::ImplementationClassUID;
		userInfo.ImpVersion_.Name = dicom::ImplementationVersionName;
		userInfo.SetMax(maxSubLength);
		userInfo.AddSCPSCURoleSelection(classUID,true,true);
		userInfo.SetAsynchronousOperationsWindow(4,2);
		std::vector<BYTE> applicationInfo;
		applicationInfo.push_back(0x01);
		userInfo.AddSOPClassExtendedNegotiation(classUID,applicationInfo);

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::ClientConnection client(
					"127.0.0.1",
					port,
					"SCU_AE",
					"SCP_AE",
					contexts,
					userInfo);
				assert(client.CanActAsSCU(classUID));
				assert(client.CanActAsSCP(classUID));
				assert(client.HasNegotiatedAsynchronousOperationsWindow());
				assert(client.MaximumNumberOperationsInvoked() == 1);
				assert(client.MaximumNumberOperationsPerformed() == 1);
				assert(client.HasNegotiatedSOPClassExtended(classUID));
				assert(client.GetNegotiatedSOPClassExtendedInformation(classUID).size() == 1);
				assert(client.GetNegotiatedSOPClassExtendedInformation(classUID).at(0) == 0x09);
				completed = extendedNegotiationHandled;
			}
			catch(const SystemError&)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}

		server.Stop();
		assert(completed);
		assert(extendedNegotiationHandled);
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
		dicom::primitive::UserInformation userInfo = makeUserInformation();
		userInfo.AddSCPSCURoleSelection(getClassUID,true,false);
		userInfo.AddSCPSCURoleSelection(storeClassUID,false,true);

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts, userInfo);
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
		dicom::primitive::UserInformation userInfo = makeUserInformation();
		userInfo.AddSCPSCURoleSelection(getClassUID,true,false);
		userInfo.AddSCPSCURoleSelection(storeClassUID,false,true);

		bool completed = false;
		for(int attempt = 0; attempt < 20 && !completed; ++attempt)
		{
			try
			{
				dicom::DataSet query;
				query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
				query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);

				dicom::ClientConnection client("127.0.0.1", port, "SCU_AE", "SCP_AE", contexts, userInfo);
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
	checkNdimseCommandSets();
	checkCdimseStatusHelpers();
	checkNdimseStatusHelpers();
	checkNdimseSCUOverPData();
	checkNdimseSCUValidation();
	checkNdimseSCPOverPData();
	checkCdimseRoleEnforcement();
	checkCCancel();
	checkCCancelOverPData();
	checkSCUResponseValidationOverPData();
	checkCdimseSCPRequestValidation();
	checkAssociationExtendedNegotiation();
	checkAssociationNegotiationAndCEcho();
	checkServerClientCEcho();
	checkServerClientNdimseDispatch();
	checkServerClientExtendedNegotiationState();
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
