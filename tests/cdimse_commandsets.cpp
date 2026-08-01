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
#include <functional>
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

	void assertTwoAttributeIdentifiers(const dicom::DataSet& data)
	{
		std::vector<dicom::Value> attrValues = data.Values(dicom::TAG_ATTR_ID_LIST);
		assert(attrValues.size() == 2);
		dicom::Tag attrTag = dicom::TAG_NULL;
		attrValues.at(0) >> attrTag;
		assert(attrTag == dicom::TAG_PAT_NAME);
		attrValues.at(1) >> attrTag;
		assert(attrTag == dicom::TAG_PAT_ID);
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

	void checkImplementationIdentityDefaults()
	{
		const dicom::UID expectedImplementationClassUID("1.2.826.0.1.3680043.10.1778");
		assert(dicom::ImplementationClassUID == expectedImplementationClassUID.str());

		dicom::primitive::UserInformation userInfo = makeUserInformation();
		assert(userInfo.ImpClass_.UID_ == expectedImplementationClassUID);
		assert(userInfo.ImpVersion_.Name == dicom::ImplementationVersionName);
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

		const dicom::UID firstClassUID("1.2.826.0.1.3680043.10.1553.2.1");
		const dicom::UID secondClassUID("1.2.826.0.1.3680043.10.1553.2.2");
		dicom::PresentationContexts twoContexts;
		twoContexts.Add(firstClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
		twoContexts.Add(secondClassUID, dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));

		dicom::primitive::PresentationContextAccept rejectedFirstContext;
		rejectedFirstContext.PresentationContextID_ = twoContexts.at(0).ID_;
		rejectedFirstContext.Result_ = 1;
		rejectedFirstContext.TrnSyntax_ =
			dicom::primitive::TransferSyntax(dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		dicom::primitive::PresentationContextAccept acceptedSecondContext;
		acceptedSecondContext.PresentationContextID_ = twoContexts.at(1).ID_;
		acceptedSecondContext.Result_ = 0;
		acceptedSecondContext.TrnSyntax_ =
			dicom::primitive::TransferSyntax(dicom::IMPL_VR_LE_TRANSFER_SYNTAX);

		dicom::primitive::AAssociateAC reorderedAcknowledgement;
		reorderedAcknowledgement.PresContextAccepts_.push_back(acceptedSecondContext);
		reorderedAcknowledgement.PresContextAccepts_.push_back(rejectedFirstContext);
		reorderedAcknowledgement.UserInfo_.AddSCPSCURoleSelection(firstClassUID,true,false);
		reorderedAcknowledgement.UserInfo_.AddSCPSCURoleSelection(secondClassUID,true,false);

		NullService reorderedRequestorState;
		reorderedRequestorState.AAssociateRQ_.ProposedPresentationContexts_ = twoContexts;
		reorderedRequestorState.AcceptedPresentationContexts_ =
			reorderedAcknowledgement.PresContextAccepts_;
		reorderedRequestorState.ApplyAssociationNegotiationAsRequestor(reorderedAcknowledgement);
		assert(!reorderedRequestorState.HasNegotiatedRole(firstClassUID));
		assert(reorderedRequestorState.CanActAsSCU(secondClassUID));
		assert(reorderedRequestorState.FindPresentationContextID(firstClassUID) == 0);
		assert(reorderedRequestorState.FindPresentationContextID(secondClassUID) ==
			twoContexts.at(1).ID_);
		assert(reorderedRequestorState.GetPresentationContextID(secondClassUID) ==
			twoContexts.at(1).ID_);
		assert(reorderedRequestorState.GetPresentationContextID(
			secondClassUID,
			dicom::IMPL_VR_LE_TRANSFER_SYNTAX) == twoContexts.at(1).ID_);
		bool rejectedContextLookupRejected = false;
		try
		{
			reorderedRequestorState.GetPresentationContextID(firstClassUID);
		}
		catch(const dicom::exception&)
		{
			rejectedContextLookupRejected = true;
		}
		assert(rejectedContextLookupRejected);
		bool rejectedContextTransferSyntaxLookupRejected = false;
		try
		{
			reorderedRequestorState.GetPresentationContextID(
				firstClassUID,
				dicom::IMPL_VR_LE_TRANSFER_SYNTAX);
		}
		catch(const dicom::exception&)
		{
			rejectedContextTransferSyntaxLookupRejected = true;
		}
		assert(rejectedContextTransferSyntaxLookupRejected);

		NullService reorderedAcceptorState;
		reorderedAcceptorState.AAssociateRQ_.ProposedPresentationContexts_ = twoContexts;
		reorderedAcceptorState.AcceptedPresentationContexts_ =
			reorderedAcknowledgement.PresContextAccepts_;
		reorderedAcceptorState.ApplyAssociationNegotiationAsAcceptor(reorderedAcknowledgement.UserInfo_);
		assert(!reorderedAcceptorState.HasNegotiatedRole(firstClassUID));
		assert(reorderedAcceptorState.CanActAsSCP(secondClassUID));
		assert(reorderedAcceptorState.FindPresentationContextID(firstClassUID) == 0);
		assert(reorderedAcceptorState.FindPresentationContextID(secondClassUID) ==
			twoContexts.at(1).ID_);
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

		dicom::CommandSet::NEventReportRSP eventInvalidArgumentRSP(
			21,
			classUID,
			instUID,
			UINT16(0x0115),
			eventTypeID,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(eventInvalidArgumentRSP, dicom::TAG_STATUS) == 0x0115);
		assert(get<UINT16>(eventInvalidArgumentRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(eventInvalidArgumentRSP, dicom::TAG_EVENT_TYPE_ID) == eventTypeID);

		dicom::CommandSet::NEventReportRSP eventNoSuchEventTypeRSP(
			21,
			classUID,
			instUID,
			UINT16(0x0113),
			eventTypeID,
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(eventNoSuchEventTypeRSP, dicom::TAG_STATUS) == 0x0113);
		assert(get<UINT16>(eventNoSuchEventTypeRSP, dicom::TAG_EVENT_TYPE_ID) == eventTypeID);

		dicom::CommandSet::NEventReportRSP eventNoSuchArgumentRSP(
			21,
			classUID,
			instUID,
			UINT16(0x0114),
			eventTypeID,
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(eventNoSuchArgumentRSP, dicom::TAG_STATUS) == 0x0114);
		assert(get<UINT16>(eventNoSuchArgumentRSP, dicom::TAG_EVENT_TYPE_ID) == eventTypeID);

		std::vector<dicom::Tag> attrList;
		attrList.push_back(dicom::TAG_PAT_NAME);
		attrList.push_back(dicom::TAG_PAT_ID);
		dicom::CommandSet::NGetRQ getRQ(23,classUID,instUID,attrList);
		assert(get<dicom::UID>(getRQ, dicom::TAG_REQ_SOP_CLASS_UID) == classUID);
		assert(get<dicom::UID>(getRQ, dicom::TAG_REQ_SOP_INST_UID) == instUID);
		assert(get<UINT16>(getRQ, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RQ);
		assert(get<UINT16>(getRQ, dicom::TAG_MSG_ID) == 23);
		assert(get<UINT16>(getRQ, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::NO_DATA_SET);
		assertTwoAttributeIdentifiers(getRQ);
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

		dicom::CommandSet::NGetRSP getRSPWithAttrList(
			23,
			classUID,
			instUID,
			0x0120,
			dicom::DataSetStatus::NO_DATA_SET,
			attrList);
		assert(get<UINT16>(getRSPWithAttrList, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
		assert(get<UINT16>(getRSPWithAttrList, dicom::TAG_STATUS) == 0x0120);
		assertTwoAttributeIdentifiers(getRSPWithAttrList);

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

		dicom::CommandSet::NSetRSP setRSPWithAttrList(
			25,
			classUID,
			instUID,
			0x0105,
			dicom::DataSetStatus::NO_DATA_SET,
			attrList);
		assert(get<UINT16>(setRSPWithAttrList, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
		assert(get<UINT16>(setRSPWithAttrList, dicom::TAG_STATUS) == 0x0105);
		assertTwoAttributeIdentifiers(setRSPWithAttrList);

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

		dicom::CommandSet::NActionRSP actionInvalidArgumentRSP(
			27,
			classUID,
			instUID,
			UINT16(0x0115),
			actionTypeID,
			dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(actionInvalidArgumentRSP, dicom::TAG_STATUS) == 0x0115);
		assert(get<UINT16>(actionInvalidArgumentRSP, dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
		assert(get<UINT16>(actionInvalidArgumentRSP, dicom::TAG_ACTION_TYPE_ID) == actionTypeID);

		dicom::CommandSet::NActionRSP actionNoSuchArgumentRSP(
			27,
			classUID,
			instUID,
			UINT16(0x0114),
			actionTypeID,
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(actionNoSuchArgumentRSP, dicom::TAG_STATUS) == 0x0114);
		assert(get<UINT16>(actionNoSuchArgumentRSP, dicom::TAG_ACTION_TYPE_ID) == actionTypeID);

		dicom::CommandSet::NActionRSP actionNoSuchActionTypeRSP(
			27,
			classUID,
			instUID,
			UINT16(0x0123),
			actionTypeID,
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<UINT16>(actionNoSuchActionTypeRSP, dicom::TAG_STATUS) == 0x0123);
		assert(get<UINT16>(actionNoSuchActionTypeRSP, dicom::TAG_ACTION_TYPE_ID) == actionTypeID);

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

		dicom::CommandSet::NCreateRSP createDuplicateRSP(
			29,
			classUID,
			instUID,
			UINT16(0x0111),
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<dicom::UID>(createDuplicateRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(createDuplicateRSP, dicom::TAG_STATUS) == 0x0111);

		dicom::CommandSet::NCreateRSP createDuplicateRSPWithoutUID(
			29,
			classUID,
			dicom::UID(""),
			UINT16(0x0111),
			dicom::DataSetStatus::NO_DATA_SET);
		assert(!createDuplicateRSPWithoutUID.exists(dicom::TAG_AFF_SOP_INST_UID));
		assert(get<UINT16>(createDuplicateRSPWithoutUID, dicom::TAG_STATUS) == 0x0111);

		dicom::CommandSet::NCreateRSP createInvalidObjectInstanceRSP(
			29,
			classUID,
			instUID,
			UINT16(0x0117),
			dicom::DataSetStatus::NO_DATA_SET);
		assert(get<dicom::UID>(createInvalidObjectInstanceRSP, dicom::TAG_AFF_SOP_INST_UID) == instUID);
		assert(get<UINT16>(createInvalidObjectInstanceRSP, dicom::TAG_STATUS) == 0x0117);

		dicom::CommandSet::NCreateRSP createRSPWithAttrList(
			29,
			classUID,
			instUID,
			0x0120,
			dicom::DataSetStatus::NO_DATA_SET,
			attrList);
		assert(get<UINT16>(createRSPWithAttrList, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
		assert(get<UINT16>(createRSPWithAttrList, dicom::TAG_STATUS) == 0x0120);
		assertTwoAttributeIdentifiers(createRSPWithAttrList);

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

			dicom::DataSet requestData;
			requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
			dicom::NSetSCU scu(scuService,classUID);
			scu.writeRQ(instUID,requestData);

			dicom::DataSet command;
			requireRead(scpService,command);
			const UINT16 messageID = get<UINT16>(command, dicom::TAG_MSG_ID);
			dicom::DataSet readRequestData;
			requireRead(scpService,readRequestData);
			assert(get<dicom::UID>(readRequestData, dicom::TAG_SOP_INST_UID) == instUID);

			dicom::CommandSet::NSetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				0x0106,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet responseData;
			responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0106"));
			scpService.WriteCommand(responseCommand,classUID);
			scpService.WriteDataSet(responseData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == 0x0106);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<std::string>(data, dicom::TAG_PAT_ID) == "NSET0106");
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
			dicom::DataSet readRequestData;
			requireRead(scpService,readRequestData);
			assert(get<dicom::UID>(readRequestData, dicom::TAG_SOP_INST_UID) == instUID);

			dicom::CommandSet::NSetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				0x0116,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet responseData;
			responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0116"));
			scpService.WriteCommand(responseCommand,classUID);
			scpService.WriteDataSet(responseData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == 0x0116);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<std::string>(data, dicom::TAG_PAT_ID) == "NSET0116");
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
			dicom::DataSet readRequestData;
			requireRead(scpService,readRequestData);
			assert(get<dicom::UID>(readRequestData, dicom::TAG_SOP_INST_UID) == instUID);

			dicom::CommandSet::NSetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				0x0121,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet responseData;
			responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0121"));
			scpService.WriteCommand(responseCommand,classUID);
			scpService.WriteDataSet(responseData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == 0x0121);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<std::string>(data, dicom::TAG_PAT_ID) == "NSET0121");
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

		{
			int eventTypeMismatchSockets[2];
			makeSocketPair(eventTypeMismatchSockets);
			PairedService eventTypeMismatchSCUService(eventTypeMismatchSockets[0], classUID);
			PairedService eventTypeMismatchSCPService(eventTypeMismatchSockets[1], classUID);

			dicom::NEventReportSCU eventTypeMismatchSCU(eventTypeMismatchSCUService,classUID);
			eventTypeMismatchSCU.writeRQ(instUID,3);

			dicom::DataSet eventTypeMismatchRequest;
			requireRead(eventTypeMismatchSCPService,eventTypeMismatchRequest);
			const UINT16 eventTypeMismatchMessageID = get<UINT16>(eventTypeMismatchRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP eventTypeMismatchResponse(
				eventTypeMismatchMessageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				4,
				dicom::DataSetStatus::NO_DATA_SET);
			eventTypeMismatchSCPService.WriteCommand(eventTypeMismatchResponse,classUID);

			bool eventTypeMismatchRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				eventTypeMismatchSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				eventTypeMismatchRejected = true;
			}
		if(!eventTypeMismatchRejected)
			throw dicom::exception("N-DIMSE response Event Type ID mismatch was not rejected");
		}

		const auto assertEventTypeMismatchRejected =
			[&](UINT16 responseStatus)
			{
				int eventTypeMismatchSockets[2];
				makeSocketPair(eventTypeMismatchSockets);
				PairedService eventTypeMismatchSCUService(eventTypeMismatchSockets[0], classUID);
				PairedService eventTypeMismatchSCPService(eventTypeMismatchSockets[1], classUID);

				dicom::NEventReportSCU eventTypeMismatchSCU(eventTypeMismatchSCUService,classUID);
				eventTypeMismatchSCU.writeRQ(instUID,3);

				dicom::DataSet eventTypeMismatchRequest;
				requireRead(eventTypeMismatchSCPService,eventTypeMismatchRequest);
				const UINT16 eventTypeMismatchMessageID =
					get<UINT16>(eventTypeMismatchRequest, dicom::TAG_MSG_ID);
				dicom::CommandSet::NEventReportRSP eventTypeMismatchResponse(
					eventTypeMismatchMessageID,
					classUID,
					instUID,
					responseStatus,
					4,
					dicom::DataSetStatus::NO_DATA_SET);
				eventTypeMismatchSCPService.WriteCommand(eventTypeMismatchResponse,classUID);

				bool rejected = false;
				try
				{
					UINT16 status = 0;
					dicom::DataSet response;
					dicom::DataSet data;
					eventTypeMismatchSCU.readRSP(status,response,data);
				}
				catch(const dicom::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		assertEventTypeMismatchRejected(0x0113);
		assertEventTypeMismatchRejected(0x0114);
		assertEventTypeMismatchRejected(0x0115);

		{
			int actionTypeMismatchSockets[2];
			makeSocketPair(actionTypeMismatchSockets);
			PairedService actionTypeMismatchSCUService(actionTypeMismatchSockets[0], classUID);
			PairedService actionTypeMismatchSCPService(actionTypeMismatchSockets[1], classUID);

			dicom::NActionSCU actionTypeMismatchSCU(actionTypeMismatchSCUService,classUID);
			actionTypeMismatchSCU.writeRQ(instUID,7);

			dicom::DataSet actionTypeMismatchRequest;
			requireRead(actionTypeMismatchSCPService,actionTypeMismatchRequest);
			const UINT16 actionTypeMismatchMessageID = get<UINT16>(actionTypeMismatchRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP actionTypeMismatchResponse(
				actionTypeMismatchMessageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				8,
				dicom::DataSetStatus::NO_DATA_SET);
			actionTypeMismatchSCPService.WriteCommand(actionTypeMismatchResponse,classUID);

			bool actionTypeMismatchRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				actionTypeMismatchSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				actionTypeMismatchRejected = true;
			}
		if(!actionTypeMismatchRejected)
			throw dicom::exception("N-DIMSE response Action Type ID mismatch was not rejected");
		}

		const auto assertActionTypeMismatchRejected =
			[&](UINT16 responseStatus)
			{
				int actionTypeMismatchSockets[2];
				makeSocketPair(actionTypeMismatchSockets);
				PairedService actionTypeMismatchSCUService(actionTypeMismatchSockets[0], classUID);
				PairedService actionTypeMismatchSCPService(actionTypeMismatchSockets[1], classUID);

				dicom::NActionSCU actionTypeMismatchSCU(actionTypeMismatchSCUService,classUID);
				actionTypeMismatchSCU.writeRQ(instUID,7);

				dicom::DataSet actionTypeMismatchRequest;
				requireRead(actionTypeMismatchSCPService,actionTypeMismatchRequest);
				const UINT16 actionTypeMismatchMessageID =
					get<UINT16>(actionTypeMismatchRequest, dicom::TAG_MSG_ID);
				dicom::CommandSet::NActionRSP actionTypeMismatchResponse(
					actionTypeMismatchMessageID,
					classUID,
					instUID,
					responseStatus,
					8,
					dicom::DataSetStatus::NO_DATA_SET);
				actionTypeMismatchSCPService.WriteCommand(actionTypeMismatchResponse,classUID);

				bool rejected = false;
				try
				{
					UINT16 status = 0;
					dicom::DataSet response;
					dicom::DataSet data;
					actionTypeMismatchSCU.readRSP(status,response,data);
				}
				catch(const dicom::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		assertActionTypeMismatchRejected(0x0114);
		assertActionTypeMismatchRejected(0x0115);
		assertActionTypeMismatchRejected(0x0123);

		{
			int instanceMismatchSockets[2];
			makeSocketPair(instanceMismatchSockets);
			PairedService instanceMismatchSCUService(instanceMismatchSockets[0], classUID);
			PairedService instanceMismatchSCPService(instanceMismatchSockets[1], classUID);
			const dicom::UID wrongInstUID("1.2.826.0.1.3680043.10.1553.22.99");

			dicom::NDeleteSCU instanceMismatchSCU(instanceMismatchSCUService,classUID);
			instanceMismatchSCU.writeRQ(instUID);

			dicom::DataSet instanceMismatchRequest;
			requireRead(instanceMismatchSCPService,instanceMismatchRequest);
			const UINT16 instanceMismatchMessageID = get<UINT16>(instanceMismatchRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NDeleteRSP instanceMismatchResponse(
				instanceMismatchMessageID,
				classUID,
				wrongInstUID,
				dicom::Status::SUCCESS);
			instanceMismatchSCPService.WriteCommand(instanceMismatchResponse,classUID);

			bool instanceMismatchRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				instanceMismatchSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				instanceMismatchRejected = true;
			}
			if(!instanceMismatchRejected)
				throw dicom::exception("N-DIMSE response SOP Instance UID mismatch was not rejected");
		}

		{
			int createFailureSockets[2];
			makeSocketPair(createFailureSockets);
			PairedService createFailureSCUService(createFailureSockets[0], classUID);
			PairedService createFailureSCPService(createFailureSockets[1], classUID);

			dicom::NCreateSCU createFailureSCU(createFailureSCUService,classUID);
			createFailureSCU.writeRQ(instUID);

			dicom::DataSet createFailureRequest;
			requireRead(createFailureSCPService,createFailureRequest);
			const UINT16 createFailureMessageID = get<UINT16>(createFailureRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createFailureResponse;
			createFailureResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createFailureResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createFailureResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createFailureMessageID);
			createFailureResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::NO_DATA_SET);
			createFailureResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0110));
			createFailureResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			createFailureSCPService.WriteCommand(createFailureResponse,classUID);

			bool createFailureRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				createFailureSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				createFailureRejected = true;
			}
			if(!createFailureRejected)
				throw dicom::exception("N-CREATE non-success response SOP Instance UID was not rejected");
		}

		{
			int createDuplicateSockets[2];
			makeSocketPair(createDuplicateSockets);
			PairedService createDuplicateSCUService(createDuplicateSockets[0], classUID);
			PairedService createDuplicateSCPService(createDuplicateSockets[1], classUID);

			dicom::NCreateSCU createDuplicateSCU(createDuplicateSCUService,classUID);
			createDuplicateSCU.writeRQ(instUID);

			dicom::DataSet createDuplicateRequest;
			requireRead(createDuplicateSCPService,createDuplicateRequest);
			const UINT16 createDuplicateMessageID = get<UINT16>(createDuplicateRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createDuplicateResponse;
			createDuplicateResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createDuplicateResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createDuplicateResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createDuplicateMessageID);
			createDuplicateResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::NO_DATA_SET);
			createDuplicateResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0111));
			createDuplicateResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			createDuplicateSCPService.WriteCommand(createDuplicateResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			createDuplicateSCU.readRSP(status,response,data);
			assert(status == 0x0111);
			assert(get<dicom::UID>(response,dicom::TAG_AFF_SOP_INST_UID) == instUID);
			assert(data.empty());
		}

		{
			int createInvalidObjectSockets[2];
			makeSocketPair(createInvalidObjectSockets);
			PairedService createInvalidObjectSCUService(createInvalidObjectSockets[0], classUID);
			PairedService createInvalidObjectSCPService(createInvalidObjectSockets[1], classUID);

			dicom::NCreateSCU createInvalidObjectSCU(createInvalidObjectSCUService,classUID);
			createInvalidObjectSCU.writeRQ(instUID);

			dicom::DataSet createInvalidObjectRequest;
			requireRead(createInvalidObjectSCPService,createInvalidObjectRequest);
			const UINT16 createInvalidObjectMessageID =
				get<UINT16>(createInvalidObjectRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createInvalidObjectResponse;
			createInvalidObjectResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createInvalidObjectResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createInvalidObjectResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createInvalidObjectMessageID);
			createInvalidObjectResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::NO_DATA_SET);
			createInvalidObjectResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0117));
			createInvalidObjectResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			createInvalidObjectSCPService.WriteCommand(createInvalidObjectResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			createInvalidObjectSCU.readRSP(status,response,data);
			assert(status == 0x0117);
			assert(get<dicom::UID>(response,dicom::TAG_AFF_SOP_INST_UID) == instUID);
			assert(data.empty());
		}

		{
			int createDuplicateEmptyInstanceSockets[2];
			makeSocketPair(createDuplicateEmptyInstanceSockets);
			PairedService createDuplicateEmptyInstanceSCUService(createDuplicateEmptyInstanceSockets[0], classUID);
			PairedService createDuplicateEmptyInstanceSCPService(createDuplicateEmptyInstanceSockets[1], classUID);

			dicom::NCreateSCU createDuplicateEmptyInstanceSCU(createDuplicateEmptyInstanceSCUService,classUID);
			createDuplicateEmptyInstanceSCU.writeRQ(instUID);

			dicom::DataSet createDuplicateEmptyInstanceRequest;
			requireRead(createDuplicateEmptyInstanceSCPService,createDuplicateEmptyInstanceRequest);
			const UINT16 createDuplicateEmptyInstanceMessageID =
				get<UINT16>(createDuplicateEmptyInstanceRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createDuplicateEmptyInstanceResponse;
			createDuplicateEmptyInstanceResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createDuplicateEmptyInstanceResponse.Put<dicom::VR_US>(
				dicom::TAG_CMD_FIELD,
				dicom::Command::N_CREATE_RSP);
			createDuplicateEmptyInstanceResponse.Put<dicom::VR_US>(
				dicom::TAG_MSG_ID_RSP,
				createDuplicateEmptyInstanceMessageID);
			createDuplicateEmptyInstanceResponse.Put<dicom::VR_US>(
				dicom::TAG_DATA_SET_TYPE,
				dicom::DataSetStatus::NO_DATA_SET);
			createDuplicateEmptyInstanceResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0111));
			createDuplicateEmptyInstanceResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,dicom::UID(""));
			createDuplicateEmptyInstanceSCPService.WriteCommand(createDuplicateEmptyInstanceResponse,classUID);

			bool createDuplicateEmptyInstanceRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				createDuplicateEmptyInstanceSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				createDuplicateEmptyInstanceRejected = true;
			}
			if(!createDuplicateEmptyInstanceRejected)
				throw dicom::exception("N-CREATE duplicate SOP Instance empty UID was not rejected");
		}

		{
			int createInvalidObjectEmptyInstanceSockets[2];
			makeSocketPair(createInvalidObjectEmptyInstanceSockets);
			PairedService createInvalidObjectEmptyInstanceSCUService(
				createInvalidObjectEmptyInstanceSockets[0],
				classUID);
			PairedService createInvalidObjectEmptyInstanceSCPService(
				createInvalidObjectEmptyInstanceSockets[1],
				classUID);

			dicom::NCreateSCU createInvalidObjectEmptyInstanceSCU(
				createInvalidObjectEmptyInstanceSCUService,
				classUID);
			createInvalidObjectEmptyInstanceSCU.writeRQ(instUID);

			dicom::DataSet createInvalidObjectEmptyInstanceRequest;
			requireRead(createInvalidObjectEmptyInstanceSCPService,createInvalidObjectEmptyInstanceRequest);
			const UINT16 createInvalidObjectEmptyInstanceMessageID =
				get<UINT16>(createInvalidObjectEmptyInstanceRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createInvalidObjectEmptyInstanceResponse;
			createInvalidObjectEmptyInstanceResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createInvalidObjectEmptyInstanceResponse.Put<dicom::VR_US>(
				dicom::TAG_CMD_FIELD,
				dicom::Command::N_CREATE_RSP);
			createInvalidObjectEmptyInstanceResponse.Put<dicom::VR_US>(
				dicom::TAG_MSG_ID_RSP,
				createInvalidObjectEmptyInstanceMessageID);
			createInvalidObjectEmptyInstanceResponse.Put<dicom::VR_US>(
				dicom::TAG_DATA_SET_TYPE,
				dicom::DataSetStatus::NO_DATA_SET);
			createInvalidObjectEmptyInstanceResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0117));
			createInvalidObjectEmptyInstanceResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,dicom::UID(""));
			createInvalidObjectEmptyInstanceSCPService.WriteCommand(createInvalidObjectEmptyInstanceResponse,classUID);

			bool createInvalidObjectEmptyInstanceRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				createInvalidObjectEmptyInstanceSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				createInvalidObjectEmptyInstanceRejected = true;
			}
			if(!createInvalidObjectEmptyInstanceRejected)
				throw dicom::exception("N-CREATE invalid object instance empty UID was not rejected");
		}

		{
			int createFailureDataSockets[2];
			makeSocketPair(createFailureDataSockets);
			PairedService createFailureDataSCUService(createFailureDataSockets[0], classUID);
			PairedService createFailureDataSCPService(createFailureDataSockets[1], classUID);

			dicom::NCreateSCU createFailureDataSCU(createFailureDataSCUService,classUID);
			createFailureDataSCU.writeRQ(instUID);

			dicom::DataSet createFailureDataRequest;
			requireRead(createFailureDataSCPService,createFailureDataRequest);
			const UINT16 createFailureDataMessageID = get<UINT16>(createFailureDataRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createFailureDataResponse;
			createFailureDataResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createFailureDataResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createFailureDataResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createFailureDataMessageID);
			createFailureDataResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			createFailureDataResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0106));
			dicom::DataSet createFailureData;
			createFailureData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0106"));
			createFailureDataSCPService.WriteCommand(createFailureDataResponse,classUID);
			createFailureDataSCPService.WriteDataSet(createFailureData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			createFailureDataSCU.readRSP(status,response,data);
			assert(status == 0x0106);
			assert(get<std::string>(data,dicom::TAG_PAT_ID) == "NCREATE0106");
		}

		{
			int createWarningDataSockets[2];
			makeSocketPair(createWarningDataSockets);
			PairedService createWarningDataSCUService(createWarningDataSockets[0], classUID);
			PairedService createWarningDataSCPService(createWarningDataSockets[1], classUID);

			dicom::NCreateSCU createWarningDataSCU(createWarningDataSCUService,classUID);
			createWarningDataSCU.writeRQ(instUID);

			dicom::DataSet createWarningDataRequest;
			requireRead(createWarningDataSCPService,createWarningDataRequest);
			const UINT16 createWarningDataMessageID = get<UINT16>(createWarningDataRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createWarningDataResponse;
			createWarningDataResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createWarningDataResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createWarningDataResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createWarningDataMessageID);
			createWarningDataResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			createWarningDataResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0116));
			dicom::DataSet createWarningData;
			createWarningData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0116"));
			createWarningDataSCPService.WriteCommand(createWarningDataResponse,classUID);
			createWarningDataSCPService.WriteDataSet(createWarningData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			createWarningDataSCU.readRSP(status,response,data);
			assert(status == 0x0116);
			assert(get<std::string>(data,dicom::TAG_PAT_ID) == "NCREATE0116");
		}

		{
			int createMissingValueDataSockets[2];
			makeSocketPair(createMissingValueDataSockets);
			PairedService createMissingValueDataSCUService(createMissingValueDataSockets[0], classUID);
			PairedService createMissingValueDataSCPService(createMissingValueDataSockets[1], classUID);

			dicom::NCreateSCU createMissingValueDataSCU(createMissingValueDataSCUService,classUID);
			createMissingValueDataSCU.writeRQ(instUID);

			dicom::DataSet createMissingValueDataRequest;
			requireRead(createMissingValueDataSCPService,createMissingValueDataRequest);
			const UINT16 createMissingValueDataMessageID = get<UINT16>(createMissingValueDataRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createMissingValueDataResponse;
			createMissingValueDataResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createMissingValueDataResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createMissingValueDataResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createMissingValueDataMessageID);
			createMissingValueDataResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			createMissingValueDataResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0121));
			dicom::DataSet createMissingValueData;
			createMissingValueData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0121"));
			createMissingValueDataSCPService.WriteCommand(createMissingValueDataResponse,classUID);
			createMissingValueDataSCPService.WriteDataSet(createMissingValueData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			createMissingValueDataSCU.readRSP(status,response,data);
			assert(status == 0x0121);
			assert(get<std::string>(data,dicom::TAG_PAT_ID) == "NCREATE0121");
		}

		{
			int createSuccessSockets[2];
			makeSocketPair(createSuccessSockets);
			PairedService createSuccessSCUService(createSuccessSockets[0], classUID);
			PairedService createSuccessSCPService(createSuccessSockets[1], classUID);

			dicom::NCreateSCU createSuccessSCU(createSuccessSCUService,classUID);
			createSuccessSCU.writeRQ();

			dicom::DataSet createSuccessRequest;
			requireRead(createSuccessSCPService,createSuccessRequest);
			const UINT16 createSuccessMessageID = get<UINT16>(createSuccessRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createSuccessResponse;
			createSuccessResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createSuccessResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createSuccessResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createSuccessMessageID);
			createSuccessResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::NO_DATA_SET);
			createSuccessResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			createSuccessSCPService.WriteCommand(createSuccessResponse,classUID);

			bool createSuccessRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				createSuccessSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				createSuccessRejected = true;
			}
			if(!createSuccessRejected)
				throw dicom::exception("N-CREATE success response missing SOP Instance UID was not rejected");
		}

		{
			int createEmptyInstanceSockets[2];
			makeSocketPair(createEmptyInstanceSockets);
			PairedService createEmptyInstanceSCUService(createEmptyInstanceSockets[0], classUID);
			PairedService createEmptyInstanceSCPService(createEmptyInstanceSockets[1], classUID);

			dicom::NCreateSCU createEmptyInstanceSCU(createEmptyInstanceSCUService,classUID);
			createEmptyInstanceSCU.writeRQ();

			dicom::DataSet createEmptyInstanceRequest;
			requireRead(createEmptyInstanceSCPService,createEmptyInstanceRequest);
			const UINT16 createEmptyInstanceMessageID = get<UINT16>(createEmptyInstanceRequest, dicom::TAG_MSG_ID);
			dicom::DataSet createEmptyInstanceResponse;
			createEmptyInstanceResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			createEmptyInstanceResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_CREATE_RSP);
			createEmptyInstanceResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,createEmptyInstanceMessageID);
			createEmptyInstanceResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::NO_DATA_SET);
			createEmptyInstanceResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			createEmptyInstanceResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,dicom::UID(""));
			createEmptyInstanceSCPService.WriteCommand(createEmptyInstanceResponse,classUID);

			bool createEmptyInstanceRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				createEmptyInstanceSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				createEmptyInstanceRejected = true;
			}
			if(!createEmptyInstanceRejected)
				throw dicom::exception("N-CREATE success response empty SOP Instance UID was not rejected");
		}

		{
			int getSuccessSockets[2];
			makeSocketPair(getSuccessSockets);
			PairedService getSuccessSCUService(getSuccessSockets[0], classUID);
			PairedService getSuccessSCPService(getSuccessSockets[1], classUID);

			std::vector<dicom::Tag> attrList;
			attrList.push_back(dicom::TAG_PAT_NAME);
			dicom::NGetSCU getSuccessSCU(getSuccessSCUService,classUID);
			getSuccessSCU.writeRQ(instUID,attrList);

			dicom::DataSet getSuccessRequest;
			requireRead(getSuccessSCPService,getSuccessRequest);
			const UINT16 getSuccessMessageID = get<UINT16>(getSuccessRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NGetRSP getSuccessResponse(
				getSuccessMessageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			getSuccessSCPService.WriteCommand(getSuccessResponse,classUID);

			bool getSuccessRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				getSuccessSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				getSuccessRejected = true;
			}
			if(!getSuccessRejected)
				throw dicom::exception("N-GET success response without Attribute List was not rejected");
		}

		{
			int dataSetTypeSockets[2];
			makeSocketPair(dataSetTypeSockets);
			PairedService dataSetTypeSCUService(dataSetTypeSockets[0], classUID);
			PairedService dataSetTypeSCPService(dataSetTypeSockets[1], classUID);

			dicom::NDeleteSCU dataSetTypeSCU(dataSetTypeSCUService,classUID);
			dataSetTypeSCU.writeRQ(instUID);

			dicom::DataSet dataSetTypeRequest;
			requireRead(dataSetTypeSCPService,dataSetTypeRequest);
			const UINT16 dataSetTypeMessageID = get<UINT16>(dataSetTypeRequest, dicom::TAG_MSG_ID);
			dicom::DataSet dataSetTypeResponse;
			dataSetTypeResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			dataSetTypeResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			dataSetTypeResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,dataSetTypeMessageID);
			dataSetTypeResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			dataSetTypeResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			dataSetTypeResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			dataSetTypeSCPService.WriteCommand(dataSetTypeResponse,classUID);

			bool dataSetTypeRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				dataSetTypeSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				dataSetTypeRejected = true;
			}
			if(!dataSetTypeRejected)
				throw dicom::exception("N-DIMSE N-DELETE-RSP data set was not rejected");
		}

		{
			int eventDataSockets[2];
			makeSocketPair(eventDataSockets);
			PairedService eventDataSCUService(eventDataSockets[0], classUID);
			PairedService eventDataSCPService(eventDataSockets[1], classUID);

			dicom::NEventReportSCU eventDataSCU(eventDataSCUService,classUID);
			eventDataSCU.writeRQ(instUID,3);

			dicom::DataSet eventDataRequest;
			requireRead(eventDataSCPService,eventDataRequest);
			const UINT16 eventDataMessageID = get<UINT16>(eventDataRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP eventDataResponse(
				eventDataMessageID,
				classUID,
				instUID,
				0x0110,
				3,
				dicom::DataSetStatus::YES_DATA_SET);
			eventDataSCPService.WriteCommand(eventDataResponse,classUID);

			bool eventDataRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				eventDataSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				eventDataRejected = true;
			}
			if(!eventDataRejected)
				throw dicom::exception("N-EVENT-REPORT non-success response data set was not rejected");
		}

		{
			int eventReplySockets[2];
			makeSocketPair(eventReplySockets);
			PairedService eventReplySCUService(eventReplySockets[0], classUID);
			PairedService eventReplySCPService(eventReplySockets[1], classUID);

			dicom::NEventReportSCU eventReplySCU(eventReplySCUService,classUID);
			eventReplySCU.writeRQ(instUID,3);

			dicom::DataSet eventReplyRequest;
			requireRead(eventReplySCPService,eventReplyRequest);
			const UINT16 eventReplyMessageID = get<UINT16>(eventReplyRequest, dicom::TAG_MSG_ID);
			dicom::DataSet eventReplyResponse;
			eventReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			eventReplyResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_EVENT_REPORT_RSP);
			eventReplyResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,eventReplyMessageID);
			eventReplyResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			eventReplyResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			eventReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			eventReplySCPService.WriteCommand(eventReplyResponse,classUID);

			bool eventReplyRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				eventReplySCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				eventReplyRejected = true;
			}
			if(!eventReplyRejected)
				throw dicom::exception("N-EVENT-REPORT response data set without Event Type ID was not rejected");
		}

		{
			int eventInvalidArgumentReplySockets[2];
			makeSocketPair(eventInvalidArgumentReplySockets);
			PairedService eventInvalidArgumentReplySCUService(eventInvalidArgumentReplySockets[0], classUID);
			PairedService eventInvalidArgumentReplySCPService(eventInvalidArgumentReplySockets[1], classUID);

			dicom::NEventReportSCU eventInvalidArgumentReplySCU(eventInvalidArgumentReplySCUService,classUID);
			eventInvalidArgumentReplySCU.writeRQ(instUID,3);

			dicom::DataSet eventInvalidArgumentReplyRequest;
			requireRead(eventInvalidArgumentReplySCPService,eventInvalidArgumentReplyRequest);
			const UINT16 eventInvalidArgumentReplyMessageID =
				get<UINT16>(eventInvalidArgumentReplyRequest, dicom::TAG_MSG_ID);
			dicom::DataSet eventInvalidArgumentReplyResponse;
			eventInvalidArgumentReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			eventInvalidArgumentReplyResponse.Put<dicom::VR_US>(
				dicom::TAG_CMD_FIELD,
				dicom::Command::N_EVENT_REPORT_RSP);
			eventInvalidArgumentReplyResponse.Put<dicom::VR_US>(
				dicom::TAG_MSG_ID_RSP,
				eventInvalidArgumentReplyMessageID);
			eventInvalidArgumentReplyResponse.Put<dicom::VR_US>(
				dicom::TAG_DATA_SET_TYPE,
				dicom::DataSetStatus::YES_DATA_SET);
			eventInvalidArgumentReplyResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0115));
			eventInvalidArgumentReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			eventInvalidArgumentReplySCPService.WriteCommand(eventInvalidArgumentReplyResponse,classUID);

			bool eventInvalidArgumentReplyRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				eventInvalidArgumentReplySCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				eventInvalidArgumentReplyRejected = true;
			}
			if(!eventInvalidArgumentReplyRejected)
				throw dicom::exception("N-EVENT-REPORT 0115H response data set without Event Type ID was not rejected");
		}

		{
			int actionDataSockets[2];
			makeSocketPair(actionDataSockets);
			PairedService actionDataSCUService(actionDataSockets[0], classUID);
			PairedService actionDataSCPService(actionDataSockets[1], classUID);

			dicom::NActionSCU actionDataSCU(actionDataSCUService,classUID);
			actionDataSCU.writeRQ(instUID,7);

			dicom::DataSet actionDataRequest;
			requireRead(actionDataSCPService,actionDataRequest);
			const UINT16 actionDataMessageID = get<UINT16>(actionDataRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP actionDataResponse(
				actionDataMessageID,
				classUID,
				instUID,
				0x0110,
				7,
				dicom::DataSetStatus::YES_DATA_SET);
			actionDataSCPService.WriteCommand(actionDataResponse,classUID);

			bool actionDataRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				actionDataSCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				actionDataRejected = true;
			}
			if(!actionDataRejected)
				throw dicom::exception("N-ACTION non-success response data set was not rejected");
		}

		{
			int actionReplySockets[2];
			makeSocketPair(actionReplySockets);
			PairedService actionReplySCUService(actionReplySockets[0], classUID);
			PairedService actionReplySCPService(actionReplySockets[1], classUID);

			dicom::NActionSCU actionReplySCU(actionReplySCUService,classUID);
			actionReplySCU.writeRQ(instUID,7);

			dicom::DataSet actionReplyRequest;
			requireRead(actionReplySCPService,actionReplyRequest);
			const UINT16 actionReplyMessageID = get<UINT16>(actionReplyRequest, dicom::TAG_MSG_ID);
			dicom::DataSet actionReplyResponse;
			actionReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			actionReplyResponse.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_ACTION_RSP);
			actionReplyResponse.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,actionReplyMessageID);
			actionReplyResponse.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::YES_DATA_SET);
			actionReplyResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			actionReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			actionReplySCPService.WriteCommand(actionReplyResponse,classUID);

			bool actionReplyRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				actionReplySCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				actionReplyRejected = true;
			}
			if(!actionReplyRejected)
				throw dicom::exception("N-ACTION response data set without Action Type ID was not rejected");
		}

		{
			int actionInvalidArgumentReplySockets[2];
			makeSocketPair(actionInvalidArgumentReplySockets);
			PairedService actionInvalidArgumentReplySCUService(actionInvalidArgumentReplySockets[0], classUID);
			PairedService actionInvalidArgumentReplySCPService(actionInvalidArgumentReplySockets[1], classUID);

			dicom::NActionSCU actionInvalidArgumentReplySCU(actionInvalidArgumentReplySCUService,classUID);
			actionInvalidArgumentReplySCU.writeRQ(instUID,7);

			dicom::DataSet actionInvalidArgumentReplyRequest;
			requireRead(actionInvalidArgumentReplySCPService,actionInvalidArgumentReplyRequest);
			const UINT16 actionInvalidArgumentReplyMessageID =
				get<UINT16>(actionInvalidArgumentReplyRequest, dicom::TAG_MSG_ID);
			dicom::DataSet actionInvalidArgumentReplyResponse;
			actionInvalidArgumentReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			actionInvalidArgumentReplyResponse.Put<dicom::VR_US>(
				dicom::TAG_CMD_FIELD,
				dicom::Command::N_ACTION_RSP);
			actionInvalidArgumentReplyResponse.Put<dicom::VR_US>(
				dicom::TAG_MSG_ID_RSP,
				actionInvalidArgumentReplyMessageID);
			actionInvalidArgumentReplyResponse.Put<dicom::VR_US>(
				dicom::TAG_DATA_SET_TYPE,
				dicom::DataSetStatus::YES_DATA_SET);
			actionInvalidArgumentReplyResponse.Put<dicom::VR_US>(dicom::TAG_STATUS,UINT16(0x0115));
			actionInvalidArgumentReplyResponse.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			actionInvalidArgumentReplySCPService.WriteCommand(actionInvalidArgumentReplyResponse,classUID);

			bool actionInvalidArgumentReplyRejected = false;
			try
			{
				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				actionInvalidArgumentReplySCU.readRSP(status,response,data);
			}
			catch(const dicom::exception&)
			{
				actionInvalidArgumentReplyRejected = true;
			}
			if(!actionInvalidArgumentReplyRejected)
				throw dicom::exception("N-ACTION 0115H response data set without Action Type ID was not rejected");
		}

		{
			int eventInvalidArgumentSockets[2];
			makeSocketPair(eventInvalidArgumentSockets);
			PairedService eventInvalidArgumentSCUService(eventInvalidArgumentSockets[0], classUID);
			PairedService eventInvalidArgumentSCPService(eventInvalidArgumentSockets[1], classUID);

			dicom::NEventReportSCU eventInvalidArgumentSCU(eventInvalidArgumentSCUService,classUID);
			eventInvalidArgumentSCU.writeRQ(instUID,3);

			dicom::DataSet eventInvalidArgumentRequest;
			requireRead(eventInvalidArgumentSCPService,eventInvalidArgumentRequest);
			const UINT16 eventInvalidArgumentMessageID =
				get<UINT16>(eventInvalidArgumentRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP eventInvalidArgumentResponse(
				eventInvalidArgumentMessageID,
				classUID,
				instUID,
				UINT16(0x0115),
				UINT16(3),
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet eventInvalidArgumentData;
			eventInvalidArgumentData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NEVENT0115"));
			eventInvalidArgumentSCPService.WriteCommand(eventInvalidArgumentResponse,classUID);
			eventInvalidArgumentSCPService.WriteDataSet(eventInvalidArgumentData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			eventInvalidArgumentSCU.readRSP(status,response,data);
			assert(status == 0x0115);
			assert(get<std::string>(data,dicom::TAG_PAT_ID) == "NEVENT0115");
		}

		{
			int eventNoSuchEventTypeSockets[2];
			makeSocketPair(eventNoSuchEventTypeSockets);
			PairedService eventNoSuchEventTypeSCUService(eventNoSuchEventTypeSockets[0], classUID);
			PairedService eventNoSuchEventTypeSCPService(eventNoSuchEventTypeSockets[1], classUID);

			dicom::NEventReportSCU eventNoSuchEventTypeSCU(eventNoSuchEventTypeSCUService,classUID);
			eventNoSuchEventTypeSCU.writeRQ(instUID,3);

			dicom::DataSet eventNoSuchEventTypeRequest;
			requireRead(eventNoSuchEventTypeSCPService,eventNoSuchEventTypeRequest);
			const UINT16 eventNoSuchEventTypeMessageID =
				get<UINT16>(eventNoSuchEventTypeRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP eventNoSuchEventTypeResponse(
				eventNoSuchEventTypeMessageID,
				classUID,
				instUID,
				UINT16(0x0113),
				UINT16(3),
				dicom::DataSetStatus::NO_DATA_SET);
			eventNoSuchEventTypeSCPService.WriteCommand(eventNoSuchEventTypeResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			eventNoSuchEventTypeSCU.readRSP(status,response,data);
			assert(status == 0x0113);
			assert(get<UINT16>(response,dicom::TAG_EVENT_TYPE_ID) == 3);
			assert(data.empty());
		}

		{
			int eventNoSuchArgumentSockets[2];
			makeSocketPair(eventNoSuchArgumentSockets);
			PairedService eventNoSuchArgumentSCUService(eventNoSuchArgumentSockets[0], classUID);
			PairedService eventNoSuchArgumentSCPService(eventNoSuchArgumentSockets[1], classUID);

			dicom::NEventReportSCU eventNoSuchArgumentSCU(eventNoSuchArgumentSCUService,classUID);
			eventNoSuchArgumentSCU.writeRQ(instUID,3);

			dicom::DataSet eventNoSuchArgumentRequest;
			requireRead(eventNoSuchArgumentSCPService,eventNoSuchArgumentRequest);
			const UINT16 eventNoSuchArgumentMessageID =
				get<UINT16>(eventNoSuchArgumentRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP eventNoSuchArgumentResponse(
				eventNoSuchArgumentMessageID,
				classUID,
				instUID,
				UINT16(0x0114),
				UINT16(3),
				dicom::DataSetStatus::NO_DATA_SET);
			eventNoSuchArgumentSCPService.WriteCommand(eventNoSuchArgumentResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			eventNoSuchArgumentSCU.readRSP(status,response,data);
			assert(status == 0x0114);
			assert(get<UINT16>(response,dicom::TAG_EVENT_TYPE_ID) == 3);
			assert(data.empty());
		}

		{
			int actionInvalidArgumentSockets[2];
			makeSocketPair(actionInvalidArgumentSockets);
			PairedService actionInvalidArgumentSCUService(actionInvalidArgumentSockets[0], classUID);
			PairedService actionInvalidArgumentSCPService(actionInvalidArgumentSockets[1], classUID);

			dicom::NActionSCU actionInvalidArgumentSCU(actionInvalidArgumentSCUService,classUID);
			actionInvalidArgumentSCU.writeRQ(instUID,7);

			dicom::DataSet actionInvalidArgumentRequest;
			requireRead(actionInvalidArgumentSCPService,actionInvalidArgumentRequest);
			const UINT16 actionInvalidArgumentMessageID =
				get<UINT16>(actionInvalidArgumentRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP actionInvalidArgumentResponse(
				actionInvalidArgumentMessageID,
				classUID,
				instUID,
				UINT16(0x0115),
				UINT16(7),
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet actionInvalidArgumentData;
			actionInvalidArgumentData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NACTION0115"));
			actionInvalidArgumentSCPService.WriteCommand(actionInvalidArgumentResponse,classUID);
			actionInvalidArgumentSCPService.WriteDataSet(actionInvalidArgumentData,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			actionInvalidArgumentSCU.readRSP(status,response,data);
			assert(status == 0x0115);
			assert(get<std::string>(data,dicom::TAG_PAT_ID) == "NACTION0115");
		}

		{
			int actionNoSuchActionTypeSockets[2];
			makeSocketPair(actionNoSuchActionTypeSockets);
			PairedService actionNoSuchActionTypeSCUService(actionNoSuchActionTypeSockets[0], classUID);
			PairedService actionNoSuchActionTypeSCPService(actionNoSuchActionTypeSockets[1], classUID);

			dicom::NActionSCU actionNoSuchActionTypeSCU(actionNoSuchActionTypeSCUService,classUID);
			actionNoSuchActionTypeSCU.writeRQ(instUID,7);

			dicom::DataSet actionNoSuchActionTypeRequest;
			requireRead(actionNoSuchActionTypeSCPService,actionNoSuchActionTypeRequest);
			const UINT16 actionNoSuchActionTypeMessageID =
				get<UINT16>(actionNoSuchActionTypeRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP actionNoSuchActionTypeResponse(
				actionNoSuchActionTypeMessageID,
				classUID,
				instUID,
				UINT16(0x0123),
				UINT16(7),
				dicom::DataSetStatus::NO_DATA_SET);
			actionNoSuchActionTypeSCPService.WriteCommand(actionNoSuchActionTypeResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			actionNoSuchActionTypeSCU.readRSP(status,response,data);
			assert(status == 0x0123);
			assert(get<UINT16>(response,dicom::TAG_ACTION_TYPE_ID) == 7);
			assert(data.empty());
		}

		{
			int actionNoSuchArgumentSockets[2];
			makeSocketPair(actionNoSuchArgumentSockets);
			PairedService actionNoSuchArgumentSCUService(actionNoSuchArgumentSockets[0], classUID);
			PairedService actionNoSuchArgumentSCPService(actionNoSuchArgumentSockets[1], classUID);

			dicom::NActionSCU actionNoSuchArgumentSCU(actionNoSuchArgumentSCUService,classUID);
			actionNoSuchArgumentSCU.writeRQ(instUID,7);

			dicom::DataSet actionNoSuchArgumentRequest;
			requireRead(actionNoSuchArgumentSCPService,actionNoSuchArgumentRequest);
			const UINT16 actionNoSuchArgumentMessageID =
				get<UINT16>(actionNoSuchArgumentRequest, dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP actionNoSuchArgumentResponse(
				actionNoSuchArgumentMessageID,
				classUID,
				instUID,
				UINT16(0x0114),
				UINT16(7),
				dicom::DataSetStatus::NO_DATA_SET);
			actionNoSuchArgumentSCPService.WriteCommand(actionNoSuchArgumentResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			actionNoSuchArgumentSCU.readRSP(status,response,data);
			assert(status == 0x0114);
			assert(get<UINT16>(response,dicom::TAG_ACTION_TYPE_ID) == 7);
			assert(data.empty());
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

		dicom::CommandSet::NGetRQ getRequest(14,classUID,instUID,std::vector<dicom::Tag>());
		bool nGetSuccessWithoutDataRejected = false;
		try
		{
			dicom::HandleNGet(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				getRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			nGetSuccessWithoutDataRejected = true;
		}
		if(!nGetSuccessWithoutDataRejected)
			throw dicom::exception("N-GET SCP success without Attribute List was not rejected");

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::NGetRQ getAttrRequest(15,classUID,instUID,std::vector<dicom::Tag>());
			dicom::HandleNGet(
				dicom::NAttributeHandlerFunction(
					[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&,
						dicom::DataSet&, std::vector<dicom::Tag>& attrList)
					{
						attrList.push_back(dicom::TAG_PAT_NAME);
						attrList.push_back(dicom::TAG_PAT_ID);
						return UINT16(0x0120);
					}),
				scpSide,
				getAttrRequest,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0120);
			assertTwoAttributeIdentifiers(responseCommand);
		}

		dicom::DataSet createWithoutInstanceRequest;
		createWithoutInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		createWithoutInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_CREATE_RQ);
		createWithoutInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(15));
		createWithoutInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		bool nCreateSuccessWithoutInstanceRejected = false;
		try
		{
			dicom::HandleNCreate(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return dicom::Status::SUCCESS;
				},
				invalidStatusService,
				createWithoutInstanceRequest,
				classUID);
		}
		catch(const dicom::exception&)
		{
			nCreateSuccessWithoutInstanceRejected = true;
		}
		if(!nCreateSuccessWithoutInstanceRejected)
			throw dicom::exception("N-CREATE SCP success without SOP Instance UID was not rejected");

		dicom::DataSet createWithInstanceRequest;
		createWithInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		createWithInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_CREATE_RQ);
		createWithInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(16));
		createWithInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		createWithInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID, instUID);
		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::HandleNCreate(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0106"));
					return UINT16(0x0106);
				},
				scpSide,
				createWithInstanceRequest,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0106);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NCREATE0106");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::HandleNCreate(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0116"));
					return UINT16(0x0116);
				},
				scpSide,
				createWithInstanceRequest,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0116);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NCREATE0116");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::HandleNCreate(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0121"));
					return UINT16(0x0121);
				},
				scpSide,
				createWithInstanceRequest,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0121);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NCREATE0121");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::HandleNCreate(
				dicom::NCreateAttributeHandlerFunction(
					[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&,
						dicom::UID&, dicom::DataSet&, std::vector<dicom::Tag>& attrList)
					{
						attrList.push_back(dicom::TAG_PAT_NAME);
						attrList.push_back(dicom::TAG_PAT_ID);
						return UINT16(0x0120);
					}),
				scpSide,
				createWithInstanceRequest,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0120);
			assertTwoAttributeIdentifiers(responseCommand);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::HandleNCreate(
				dicom::NCreateHandlerFunction(
					[instUID](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&,
						dicom::UID& responseInstUID, dicom::DataSet&)
					{
						responseInstUID = instUID;
						return UINT16(0x0111);
					}),
				scpSide,
				createWithInstanceRequest,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0111);
			assert(get<dicom::UID>(responseCommand,dicom::TAG_AFF_SOP_INST_UID) == instUID);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::HandleNCreate(
				dicom::NCreateHandlerFunction(
					[instUID](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&,
						dicom::UID& responseInstUID, dicom::DataSet&)
					{
						responseInstUID = instUID;
						return UINT16(0x0117);
					}),
				scpSide,
				createWithInstanceRequest,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0117);
			assert(get<dicom::UID>(responseCommand,dicom::TAG_AFF_SOP_INST_UID) == instUID);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::NSetRQ setRequest(18,classUID,instUID);
			dicom::DataSet setRequestData;
			setRequestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET-RQ"));
			requestorSide.WriteCommand(setRequest,classUID);
			dicom::DataSet readSetRequest;
			requireRead(scpSide,readSetRequest);
			requestorSide.WriteDataSet(setRequestData,classUID);

			dicom::HandleNSet(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet& requestData,
					dicom::DataSet& responseData)
				{
					assert(get<std::string>(requestData,dicom::TAG_PAT_ID) == "NSET-RQ");
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0106"));
					return UINT16(0x0106);
				},
				scpSide,
				readSetRequest,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0106);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NSET0106");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::NSetRQ setRequest(20,classUID,instUID);
			dicom::DataSet setRequestData;
			setRequestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET-RQ-0116"));
			requestorSide.WriteCommand(setRequest,classUID);
			dicom::DataSet readSetRequest;
			requireRead(scpSide,readSetRequest);
			requestorSide.WriteDataSet(setRequestData,classUID);

			dicom::HandleNSet(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet& requestData,
					dicom::DataSet& responseData)
				{
					assert(get<std::string>(requestData,dicom::TAG_PAT_ID) == "NSET-RQ-0116");
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0116"));
					return UINT16(0x0116);
				},
				scpSide,
				readSetRequest,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0116);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NSET0116");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::NSetRQ setRequest(22,classUID,instUID);
			dicom::DataSet setRequestData;
			setRequestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET-RQ-0121"));
			requestorSide.WriteCommand(setRequest,classUID);
			dicom::DataSet readSetRequest;
			requireRead(scpSide,readSetRequest);
			requestorSide.WriteDataSet(setRequestData,classUID);

			dicom::HandleNSet(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet& requestData,
					dicom::DataSet& responseData)
				{
					assert(get<std::string>(requestData,dicom::TAG_PAT_ID) == "NSET-RQ-0121");
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0121"));
					return UINT16(0x0121);
				},
				scpSide,
				readSetRequest,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0121);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NSET0121");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::NSetRQ setRequest(24,classUID,instUID);
			dicom::DataSet setRequestData;
			setRequestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET-RQ-0120"));
			requestorSide.WriteCommand(setRequest,classUID);
			dicom::DataSet readSetRequest;
			requireRead(scpSide,readSetRequest);
			requestorSide.WriteDataSet(setRequestData,classUID);

			dicom::HandleNSet(
				dicom::NAttributeHandlerFunction(
					[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet& requestData,
						dicom::DataSet&, std::vector<dicom::Tag>& attrList)
					{
						assert(get<std::string>(requestData,dicom::TAG_PAT_ID) == "NSET-RQ-0120");
						attrList.push_back(dicom::TAG_PAT_NAME);
						attrList.push_back(dicom::TAG_PAT_ID);
						return UINT16(0x0120);
					}),
				scpSide,
				readSetRequest,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0120);
			assertTwoAttributeIdentifiers(responseCommand);
		}

		dicom::DataSet eventReportRequestWithData;
		eventReportRequestWithData.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		eventReportRequestWithData.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_EVENT_REPORT_RQ);
		eventReportRequestWithData.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(17));
		eventReportRequestWithData.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		eventReportRequestWithData.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID, instUID);
		eventReportRequestWithData.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID, UINT16(3));
		bool eventReportResponseDataRejected = false;
		try
		{
			dicom::HandleNEventReport(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.1553.22.3"));
					return UINT16(0x0110);
				},
				invalidStatusService,
				eventReportRequestWithData,
				classUID);
		}
		catch(const dicom::exception&)
		{
			eventReportResponseDataRejected = true;
		}
		if(!eventReportResponseDataRejected)
			throw dicom::exception("N-EVENT-REPORT non-success response data set was not rejected");

		dicom::DataSet actionRequestWithData;
		actionRequestWithData.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		actionRequestWithData.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_ACTION_RQ);
		actionRequestWithData.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(19));
		actionRequestWithData.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		actionRequestWithData.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID, instUID);
		actionRequestWithData.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID, UINT16(7));
		bool actionResponseDataRejected = false;
		try
		{
			dicom::HandleNAction(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.1553.22.4"));
					return UINT16(0x0110);
				},
				invalidStatusService,
				actionRequestWithData,
				classUID);
		}
		catch(const dicom::exception&)
		{
			actionResponseDataRejected = true;
		}
		if(!actionResponseDataRejected)
			throw dicom::exception("N-ACTION non-success response data set was not rejected");

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::HandleNEventReport(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NEVENT0115"));
					return UINT16(0x0115);
				},
				scpSide,
				eventReportRequestWithData,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0115);
			assert(get<UINT16>(responseCommand,dicom::TAG_EVENT_TYPE_ID) == 3);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NEVENT0115");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::HandleNEventReport(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return UINT16(0x0113);
				},
				scpSide,
				eventReportRequestWithData,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0113);
			assert(get<UINT16>(responseCommand,dicom::TAG_EVENT_TYPE_ID) == 3);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::HandleNEventReport(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return UINT16(0x0114);
				},
				scpSide,
				eventReportRequestWithData,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_EVENT_REPORT_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0114);
			assert(get<UINT16>(responseCommand,dicom::TAG_EVENT_TYPE_ID) == 3);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::HandleNAction(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
				{
					responseData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NACTION0115"));
					return UINT16(0x0115);
				},
				scpSide,
				actionRequestWithData,
				classUID);

			dicom::DataSet responseCommand;
			dicom::DataSet responseData;
			requireRead(requestorSide,responseCommand);
			requireRead(requestorSide,responseData);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0115);
			assert(get<UINT16>(responseCommand,dicom::TAG_ACTION_TYPE_ID) == 7);
			assert(get<std::string>(responseData,dicom::TAG_PAT_ID) == "NACTION0115");
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::HandleNAction(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return UINT16(0x0123);
				},
				scpSide,
				actionRequestWithData,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0123);
			assert(get<UINT16>(responseCommand,dicom::TAG_ACTION_TYPE_ID) == 7);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::HandleNAction(
				[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
				{
					return UINT16(0x0114);
				},
				scpSide,
				actionRequestWithData,
				classUID);

			dicom::DataSet responseCommand;
			requireRead(requestorSide,responseCommand);
			assert(get<UINT16>(responseCommand,dicom::TAG_CMD_FIELD) == dicom::Command::N_ACTION_RSP);
			assert(get<UINT16>(responseCommand,dicom::TAG_STATUS) == 0x0114);
			assert(get<UINT16>(responseCommand,dicom::TAG_ACTION_TYPE_ID) == 7);
		}

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

		dicom::NHandlerFunction instanceUIDHandler =
			[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet& responseData)
			{
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.1553.22.6"));
				return dicom::Status::SUCCESS;
			};

		dicom::DataSet eventMissingInstanceRequest;
		eventMissingInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		eventMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_EVENT_REPORT_RQ);
		eventMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(21));
		eventMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		eventMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID, UINT16(3));
		bool eventMissingInstanceRejected = false;
		try
		{
			dicom::HandleNEventReport(instanceUIDHandler,invalidStatusService,eventMissingInstanceRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			eventMissingInstanceRejected = true;
		}
		if(!eventMissingInstanceRejected)
			throw dicom::exception("N-EVENT-REPORT request missing SOP Instance UID was not rejected");

		dicom::DataSet getMissingInstanceRequest;
		getMissingInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		getMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_GET_RQ);
		getMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(23));
		getMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		bool getMissingInstanceRejected = false;
		try
		{
			dicom::HandleNGet(instanceUIDHandler,invalidStatusService,getMissingInstanceRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			getMissingInstanceRejected = true;
		}
		if(!getMissingInstanceRejected)
			throw dicom::exception("N-GET request missing SOP Instance UID was not rejected");

		dicom::DataSet setMissingInstanceRequest;
		setMissingInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		setMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_SET_RQ);
		setMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(25));
		setMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		bool setMissingInstanceRejected = false;
		try
		{
			dicom::HandleNSet(instanceUIDHandler,invalidStatusService,setMissingInstanceRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			setMissingInstanceRejected = true;
		}
		if(!setMissingInstanceRejected)
			throw dicom::exception("N-SET request missing SOP Instance UID was not rejected");

		dicom::DataSet actionMissingInstanceRequest;
		actionMissingInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		actionMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_ACTION_RQ);
		actionMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(27));
		actionMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		actionMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID, UINT16(7));
		bool actionMissingInstanceRejected = false;
		try
		{
			dicom::HandleNAction(instanceUIDHandler,invalidStatusService,actionMissingInstanceRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			actionMissingInstanceRejected = true;
		}
		if(!actionMissingInstanceRejected)
			throw dicom::exception("N-ACTION request missing SOP Instance UID was not rejected");

		dicom::DataSet deleteMissingInstanceRequest;
		deleteMissingInstanceRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		deleteMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_DELETE_RQ);
		deleteMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(29));
		deleteMissingInstanceRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		bool deleteMissingInstanceRejected = false;
		try
		{
			dicom::HandleNDelete(instanceUIDHandler,invalidStatusService,deleteMissingInstanceRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			deleteMissingInstanceRejected = true;
		}
		if(!deleteMissingInstanceRejected)
			throw dicom::exception("N-DELETE request missing SOP Instance UID was not rejected");

		dicom::DataSet eventMissingTypeRequest;
		eventMissingTypeRequest.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		eventMissingTypeRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_EVENT_REPORT_RQ);
		eventMissingTypeRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(31));
		eventMissingTypeRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		eventMissingTypeRequest.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID, instUID);
		bool eventMissingTypeRejected = false;
		try
		{
			dicom::HandleNEventReport(instanceUIDHandler,invalidStatusService,eventMissingTypeRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			eventMissingTypeRejected = true;
		}
		if(!eventMissingTypeRejected)
			throw dicom::exception("N-EVENT-REPORT request missing Event Type ID was not rejected");

		dicom::DataSet actionMissingTypeRequest;
		actionMissingTypeRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID, classUID);
		actionMissingTypeRequest.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::N_ACTION_RQ);
		actionMissingTypeRequest.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(33));
		actionMissingTypeRequest.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		actionMissingTypeRequest.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID, instUID);
		bool actionMissingTypeRejected = false;
		try
		{
			dicom::HandleNAction(instanceUIDHandler,invalidStatusService,actionMissingTypeRequest,classUID);
		}
		catch(const dicom::exception&)
		{
			actionMissingTypeRejected = true;
		}
		if(!actionMissingTypeRejected)
			throw dicom::exception("N-ACTION request missing Action Type ID was not rejected");
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
			const dicom::UID createdUID("1.2.826.0.1.3680043.10.1553.23.100");
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::DataSet requestData;
			requestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE100"));
			dicom::NCreateSCU scu(scuService,classUID);
			scu.writeRQ(requestData);

			dicom::DataSet command;
			requireRead(scpService,command);
			assert(!command.exists(dicom::TAG_AFF_SOP_INST_UID));
			dicom::HandleNCreate(
				dicom::NCreateHandlerFunction(
					[createdUID](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet& requestData,
						dicom::UID& responseInstUID, dicom::DataSet& responseData)
					{
						assert(get<std::string>(requestData, dicom::TAG_PAT_ID) == "NCREATE100");
						responseInstUID = createdUID;
						responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, createdUID);
						return dicom::Status::SUCCESS;
					}),
				scpService,
				command,
				classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
			assert(get<dicom::UID>(response, dicom::TAG_AFF_SOP_INST_UID) == createdUID);
			assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == createdUID);
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

	void checkNdimseCommandFieldMultiplicityValidation()
	{
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.31");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.31.1");
		NullService nullService;
		const dicom::NHandlerFunction noDataSuccess =
			[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
			{
				return dicom::Status::SUCCESS;
			};
		const auto assertDeleteRequestRejected =
			[&](const dicom::DataSet& command)
			{
				bool rejected = false;
				try
				{
					dicom::HandleNDelete(noDataSuccess,nullService,command,classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertEventRequestRejected =
			[&](const dicom::DataSet& command)
			{
				bool rejected = false;
				try
				{
					dicom::HandleNEventReport(noDataSuccess,nullService,command,classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertActionRequestRejected =
			[&](const dicom::DataSet& command)
			{
				bool rejected = false;
				try
				{
					dicom::HandleNAction(noDataSuccess,nullService,command,classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertGetRequestRejected =
			[&](const dicom::DataSet& command)
			{
				bool rejected = false;
				try
				{
					dicom::HandleNGet(noDataSuccess,nullService,command,classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertSetRequestRejected =
			[&](const dicom::DataSet& command)
			{
				bool rejected = false;
				try
				{
					dicom::HandleNSet(noDataSuccess,nullService,command,classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertCreateRequestRejected =
			[&](const dicom::DataSet& command)
			{
				bool rejected = false;
				try
				{
					dicom::HandleNCreate(noDataSuccess,nullService,command,classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};

		dicom::CommandSet::NEventReportRQ eventMissingCommand(
			81,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventMissingCommand.erase(dicom::TAG_CMD_FIELD);
		assertEventRequestRejected(eventMissingCommand);

		dicom::CommandSet::NEventReportRQ eventMissingClass(
			83,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventMissingClass.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertEventRequestRejected(eventMissingClass);

		dicom::CommandSet::NEventReportRQ eventMissingMessage(
			85,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventMissingMessage.erase(dicom::TAG_MSG_ID);
		assertEventRequestRejected(eventMissingMessage);

		dicom::CommandSet::NEventReportRQ eventMissingDataSetType(
			87,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventMissingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertEventRequestRejected(eventMissingDataSetType);

		dicom::CommandSet::NEventReportRQ eventMissingEventType(
			88,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventMissingEventType.erase(dicom::TAG_EVENT_TYPE_ID);
		assertEventRequestRejected(eventMissingEventType);

		dicom::CommandSet::NGetRQ getMissingCommand(
			89,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getMissingCommand.erase(dicom::TAG_CMD_FIELD);
		assertGetRequestRejected(getMissingCommand);

		dicom::CommandSet::NGetRQ getMissingClass(
			91,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getMissingClass.erase(dicom::TAG_REQ_SOP_CLASS_UID);
		assertGetRequestRejected(getMissingClass);

		dicom::CommandSet::NGetRQ getMissingMessage(
			93,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getMissingMessage.erase(dicom::TAG_MSG_ID);
		assertGetRequestRejected(getMissingMessage);

		dicom::CommandSet::NGetRQ getMissingDataSetType(
			95,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getMissingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertGetRequestRejected(getMissingDataSetType);

		dicom::CommandSet::NSetRQ setMissingCommand(97,classUID,instUID);
		setMissingCommand.erase(dicom::TAG_CMD_FIELD);
		assertSetRequestRejected(setMissingCommand);

		dicom::CommandSet::NSetRQ setMissingClass(99,classUID,instUID);
		setMissingClass.erase(dicom::TAG_REQ_SOP_CLASS_UID);
		assertSetRequestRejected(setMissingClass);

		dicom::CommandSet::NSetRQ setMissingMessage(101,classUID,instUID);
		setMissingMessage.erase(dicom::TAG_MSG_ID);
		assertSetRequestRejected(setMissingMessage);

		dicom::CommandSet::NSetRQ setMissingDataSetType(103,classUID,instUID);
		setMissingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertSetRequestRejected(setMissingDataSetType);

		dicom::CommandSet::NActionRQ actionMissingCommand(
			105,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionMissingCommand.erase(dicom::TAG_CMD_FIELD);
		assertActionRequestRejected(actionMissingCommand);

		dicom::CommandSet::NActionRQ actionMissingClass(
			107,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionMissingClass.erase(dicom::TAG_REQ_SOP_CLASS_UID);
		assertActionRequestRejected(actionMissingClass);

		dicom::CommandSet::NActionRQ actionMissingMessage(
			109,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionMissingMessage.erase(dicom::TAG_MSG_ID);
		assertActionRequestRejected(actionMissingMessage);

		dicom::CommandSet::NActionRQ actionMissingDataSetType(
			111,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionMissingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertActionRequestRejected(actionMissingDataSetType);

		dicom::CommandSet::NActionRQ actionMissingActionType(
			112,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionMissingActionType.erase(dicom::TAG_ACTION_TYPE_ID);
		assertActionRequestRejected(actionMissingActionType);

		dicom::CommandSet::NCreateRQ createMissingCommand(
			113,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createMissingCommand.erase(dicom::TAG_CMD_FIELD);
		assertCreateRequestRejected(createMissingCommand);

		dicom::CommandSet::NCreateRQ createMissingClass(
			115,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createMissingClass.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertCreateRequestRejected(createMissingClass);

		dicom::CommandSet::NCreateRQ createMissingMessage(
			117,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createMissingMessage.erase(dicom::TAG_MSG_ID);
		assertCreateRequestRejected(createMissingMessage);

		dicom::CommandSet::NCreateRQ createMissingDataSetType(
			119,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createMissingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertCreateRequestRejected(createMissingDataSetType);

		dicom::CommandSet::NDeleteRQ deleteMissingCommand(121,classUID,instUID);
		deleteMissingCommand.erase(dicom::TAG_CMD_FIELD);
		assertDeleteRequestRejected(deleteMissingCommand);

		dicom::CommandSet::NDeleteRQ deleteMissingClass(123,classUID,instUID);
		deleteMissingClass.erase(dicom::TAG_REQ_SOP_CLASS_UID);
		assertDeleteRequestRejected(deleteMissingClass);

		dicom::CommandSet::NDeleteRQ deleteMissingMessage(125,classUID,instUID);
		deleteMissingMessage.erase(dicom::TAG_MSG_ID);
		assertDeleteRequestRejected(deleteMissingMessage);

		dicom::CommandSet::NDeleteRQ deleteMissingDataSetType(127,classUID,instUID);
		deleteMissingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertDeleteRequestRejected(deleteMissingDataSetType);

		const auto assertAllNdimseRequestsRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateCommand)
			{
				dicom::CommandSet::NEventReportRQ eventRequest(
					129,
					classUID,
					instUID,
					UINT16(3),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(eventRequest);
				assertEventRequestRejected(eventRequest);

				dicom::CommandSet::NGetRQ getRequest(
					131,
					classUID,
					instUID,
					std::vector<dicom::Tag>());
				mutateCommand(getRequest);
				assertGetRequestRejected(getRequest);

				dicom::CommandSet::NSetRQ setRequest(133,classUID,instUID);
				mutateCommand(setRequest);
				assertSetRequestRejected(setRequest);

				dicom::CommandSet::NActionRQ actionRequest(
					135,
					classUID,
					instUID,
					UINT16(7),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(actionRequest);
				assertActionRequestRejected(actionRequest);

				dicom::CommandSet::NCreateRQ createRequest(
					137,
					classUID,
					instUID,
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(createRequest);
				assertCreateRequestRejected(createRequest);

				dicom::CommandSet::NDeleteRQ deleteRequest(139,classUID,instUID);
				mutateCommand(deleteRequest);
				assertDeleteRequestRejected(deleteRequest);
			};
		const auto assertAffectedClassRequestsRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateCommand)
			{
				dicom::CommandSet::NEventReportRQ eventRequest(
					141,
					classUID,
					instUID,
					UINT16(3),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(eventRequest);
				assertEventRequestRejected(eventRequest);

				dicom::CommandSet::NCreateRQ createRequest(
					143,
					classUID,
					instUID,
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(createRequest);
				assertCreateRequestRejected(createRequest);
			};
		const auto assertRequestedClassRequestsRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateCommand)
			{
				dicom::CommandSet::NGetRQ getRequest(
					145,
					classUID,
					instUID,
					std::vector<dicom::Tag>());
				mutateCommand(getRequest);
				assertGetRequestRejected(getRequest);

				dicom::CommandSet::NSetRQ setRequest(147,classUID,instUID);
				mutateCommand(setRequest);
				assertSetRequestRejected(setRequest);

				dicom::CommandSet::NActionRQ actionRequest(
					149,
					classUID,
					instUID,
					UINT16(7),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(actionRequest);
				assertActionRequestRejected(actionRequest);

				dicom::CommandSet::NDeleteRQ deleteRequest(151,classUID,instUID);
				mutateCommand(deleteRequest);
				assertDeleteRequestRejected(deleteRequest);
			};
		const auto assertAffectedInstanceRequestsRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateCommand)
			{
				dicom::CommandSet::NEventReportRQ eventRequest(
					153,
					classUID,
					instUID,
					UINT16(3),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(eventRequest);
				assertEventRequestRejected(eventRequest);

				dicom::CommandSet::NCreateRQ createRequest(
					155,
					classUID,
					instUID,
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(createRequest);
				assertCreateRequestRejected(createRequest);
			};
		const auto assertRequestedInstanceRequestsRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateCommand)
			{
				dicom::CommandSet::NGetRQ getRequest(
					157,
					classUID,
					instUID,
					std::vector<dicom::Tag>());
				mutateCommand(getRequest);
				assertGetRequestRejected(getRequest);

				dicom::CommandSet::NSetRQ setRequest(159,classUID,instUID);
				mutateCommand(setRequest);
				assertSetRequestRejected(setRequest);

				dicom::CommandSet::NActionRQ actionRequest(
					161,
					classUID,
					instUID,
					UINT16(7),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateCommand(actionRequest);
				assertActionRequestRejected(actionRequest);

				dicom::CommandSet::NDeleteRQ deleteRequest(163,classUID,instUID);
				mutateCommand(deleteRequest);
				assertDeleteRequestRejected(deleteRequest);
			};

		assertAllNdimseRequestsRejected(
			[](dicom::DataSet& command)
			{
				command.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
			});
		assertAllNdimseRequestsRejected(
			[](dicom::DataSet& command)
			{
				command.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(165));
			});
		assertAllNdimseRequestsRejected(
			[](dicom::DataSet& command)
			{
				command.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,UINT16(165));
			});
		assertAllNdimseRequestsRejected(
			[](dicom::DataSet& command)
			{
				command.Put<dicom::VR_US>(
					dicom::TAG_DATA_SET_TYPE,
					dicom::DataSetStatus::NO_DATA_SET);
			});
		assertAllNdimseRequestsRejected(
			[](dicom::DataSet& command)
			{
				command.Put<dicom::VR_LO>(dicom::TAG_PAT_ID,std::string("PATIENT"));
			});
		assertAffectedClassRequestsRejected(
			[&](dicom::DataSet& command)
			{
				command.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			});
		assertRequestedClassRequestsRejected(
			[&](dicom::DataSet& command)
			{
				command.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID,classUID);
			});
		assertAffectedInstanceRequestsRejected(
			[&](dicom::DataSet& command)
			{
				command.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			});
		assertRequestedInstanceRequestsRejected(
			[&](dicom::DataSet& command)
			{
				command.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID,instUID);
			});

		dicom::CommandSet::NEventReportRQ eventUnexpectedRequestedClass(
			165,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventUnexpectedRequestedClass.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID,classUID);
		assertEventRequestRejected(eventUnexpectedRequestedClass);

		dicom::CommandSet::NCreateRQ createUnexpectedRequestedClass(
			166,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createUnexpectedRequestedClass.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID,classUID);
		assertCreateRequestRejected(createUnexpectedRequestedClass);

		dicom::CommandSet::NEventReportRQ eventUnexpectedRequestedInstance(
			168,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventUnexpectedRequestedInstance.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID,instUID);
		assertEventRequestRejected(eventUnexpectedRequestedInstance);

		dicom::CommandSet::NCreateRQ createUnexpectedRequestedInstance(
			170,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createUnexpectedRequestedInstance.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID,instUID);
		assertCreateRequestRejected(createUnexpectedRequestedInstance);

		dicom::CommandSet::NGetRQ getUnexpectedAffectedClass(
			172,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getUnexpectedAffectedClass.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
		assertGetRequestRejected(getUnexpectedAffectedClass);

		dicom::CommandSet::NSetRQ setUnexpectedAffectedClass(174,classUID,instUID);
		setUnexpectedAffectedClass.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
		assertSetRequestRejected(setUnexpectedAffectedClass);

		dicom::CommandSet::NActionRQ actionUnexpectedAffectedClass(
			176,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionUnexpectedAffectedClass.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
		assertActionRequestRejected(actionUnexpectedAffectedClass);

		dicom::CommandSet::NDeleteRQ deleteUnexpectedAffectedClass(178,classUID,instUID);
		deleteUnexpectedAffectedClass.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
		assertDeleteRequestRejected(deleteUnexpectedAffectedClass);

		dicom::CommandSet::NGetRQ getUnexpectedAffectedInstance(
			180,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getUnexpectedAffectedInstance.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
		assertGetRequestRejected(getUnexpectedAffectedInstance);

		dicom::CommandSet::NSetRQ setUnexpectedAffectedInstance(182,classUID,instUID);
		setUnexpectedAffectedInstance.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
		assertSetRequestRejected(setUnexpectedAffectedInstance);

		dicom::CommandSet::NActionRQ actionUnexpectedAffectedInstance(
			184,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionUnexpectedAffectedInstance.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
		assertActionRequestRejected(actionUnexpectedAffectedInstance);

		dicom::CommandSet::NDeleteRQ deleteUnexpectedAffectedInstance(186,classUID,instUID);
		deleteUnexpectedAffectedInstance.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
		assertDeleteRequestRejected(deleteUnexpectedAffectedInstance);

		dicom::CommandSet::NGetRQ getUnexpectedEventType(
			189,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getUnexpectedEventType.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
		assertGetRequestRejected(getUnexpectedEventType);

		dicom::CommandSet::NSetRQ setUnexpectedEventType(191,classUID,instUID);
		setUnexpectedEventType.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
		assertSetRequestRejected(setUnexpectedEventType);

		dicom::CommandSet::NActionRQ actionUnexpectedEventType(
			193,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionUnexpectedEventType.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
		assertActionRequestRejected(actionUnexpectedEventType);

		dicom::CommandSet::NCreateRQ createUnexpectedEventType(
			195,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createUnexpectedEventType.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
		assertCreateRequestRejected(createUnexpectedEventType);

		dicom::CommandSet::NDeleteRQ deleteUnexpectedEventType(197,classUID,instUID);
		deleteUnexpectedEventType.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
		assertDeleteRequestRejected(deleteUnexpectedEventType);

		dicom::CommandSet::NEventReportRQ eventUnexpectedActionType(
			199,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventUnexpectedActionType.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
		assertEventRequestRejected(eventUnexpectedActionType);

		dicom::CommandSet::NGetRQ getUnexpectedActionType(
			201,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getUnexpectedActionType.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
		assertGetRequestRejected(getUnexpectedActionType);

		dicom::CommandSet::NSetRQ setUnexpectedActionType(203,classUID,instUID);
		setUnexpectedActionType.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
		assertSetRequestRejected(setUnexpectedActionType);

		dicom::CommandSet::NCreateRQ createUnexpectedActionType(
			205,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createUnexpectedActionType.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
		assertCreateRequestRejected(createUnexpectedActionType);

		dicom::CommandSet::NDeleteRQ deleteUnexpectedActionType(207,classUID,instUID);
		deleteUnexpectedActionType.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
		assertDeleteRequestRejected(deleteUnexpectedActionType);

		dicom::CommandSet::NEventReportRQ eventWrongCommand(
			167,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		eventWrongCommand.erase(dicom::TAG_CMD_FIELD);
		eventWrongCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
		assertEventRequestRejected(eventWrongCommand);

		dicom::CommandSet::NGetRQ getWrongCommand(
			169,
			classUID,
			instUID,
			std::vector<dicom::Tag>());
		getWrongCommand.erase(dicom::TAG_CMD_FIELD);
		getWrongCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
		assertGetRequestRejected(getWrongCommand);

		dicom::CommandSet::NSetRQ setWrongCommand(171,classUID,instUID);
		setWrongCommand.erase(dicom::TAG_CMD_FIELD);
		setWrongCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
		assertSetRequestRejected(setWrongCommand);

		dicom::CommandSet::NActionRQ actionWrongCommand(
			173,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		actionWrongCommand.erase(dicom::TAG_CMD_FIELD);
		actionWrongCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
		assertActionRequestRejected(actionWrongCommand);

		dicom::CommandSet::NCreateRQ createWrongCommand(
			175,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		createWrongCommand.erase(dicom::TAG_CMD_FIELD);
		createWrongCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
		assertCreateRequestRejected(createWrongCommand);

		dicom::CommandSet::NDeleteRQ deleteWrongCommand(177,classUID,instUID);
		deleteWrongCommand.erase(dicom::TAG_CMD_FIELD);
		deleteWrongCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_GET_RQ);
		assertDeleteRequestRejected(deleteWrongCommand);

		const dicom::UID wrongClassUID("1.2.826.0.1.3680043.10.1553.31.98");
		assertAffectedClassRequestsRejected(
			[wrongClassUID](dicom::DataSet& command)
			{
				command.erase(dicom::TAG_AFF_SOP_CLASS_UID);
				command.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,wrongClassUID);
			});
		assertRequestedClassRequestsRejected(
			[wrongClassUID](dicom::DataSet& command)
			{
				command.erase(dicom::TAG_REQ_SOP_CLASS_UID);
				command.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID,wrongClassUID);
			});

		dicom::CommandSet::NEventReportRQ eventEmptyInstance(
			179,
			classUID,
			dicom::UID(""),
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		assertEventRequestRejected(eventEmptyInstance);

		dicom::CommandSet::NGetRQ getEmptyInstance(
			181,
			classUID,
			dicom::UID(""),
			std::vector<dicom::Tag>());
		assertGetRequestRejected(getEmptyInstance);

		dicom::CommandSet::NSetRQ setEmptyInstance(183,classUID,dicom::UID(""));
		assertSetRequestRejected(setEmptyInstance);

		dicom::CommandSet::NActionRQ actionEmptyInstance(
			185,
			classUID,
			dicom::UID(""),
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		assertActionRequestRejected(actionEmptyInstance);

		dicom::CommandSet::NDeleteRQ deleteEmptyInstance(187,classUID,dicom::UID(""));
		assertDeleteRequestRejected(deleteEmptyInstance);

		dicom::CommandSet::NDeleteRQ duplicateCommand(101,classUID,instUID);
		duplicateCommand.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RQ);
		assertDeleteRequestRejected(duplicateCommand);

		dicom::CommandSet::NDeleteRQ duplicateMessageID(103,classUID,instUID);
		duplicateMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(103));
		assertDeleteRequestRejected(duplicateMessageID);

		dicom::CommandSet::NDeleteRQ duplicateClassUID(105,classUID,instUID);
		duplicateClassUID.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID,classUID);
		assertDeleteRequestRejected(duplicateClassUID);

		dicom::CommandSet::NDeleteRQ duplicateInstanceUID(107,classUID,instUID);
		duplicateInstanceUID.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID,instUID);
		assertDeleteRequestRejected(duplicateInstanceUID);

		dicom::CommandSet::NDeleteRQ duplicateDataSetType(109,classUID,instUID);
		duplicateDataSetType.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE,dicom::DataSetStatus::NO_DATA_SET);
		assertDeleteRequestRejected(duplicateDataSetType);

		dicom::CommandSet::NEventReportRQ duplicateEventType(
			111,
			classUID,
			instUID,
			UINT16(3),
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateEventType.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
		assertEventRequestRejected(duplicateEventType);

		dicom::CommandSet::NActionRQ duplicateActionType(
			113,
			classUID,
			instUID,
			UINT16(7),
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateActionType.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
		assertActionRequestRejected(duplicateActionType);

		dicom::CommandSet::NCreateRQ duplicateCreateInstance(
			115,
			classUID,
			instUID,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateCreateInstance.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
		assertCreateRequestRejected(duplicateCreateInstance);

		dicom::CommandSet::NCreateRQ emptyCreateInstance(
			117,
			classUID,
			dicom::UID(""),
			dicom::DataSetStatus::NO_DATA_SET);
		assertCreateRequestRejected(emptyCreateInstance);

		const auto assertDeleteResponseRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuService(sockets[0], classUID);
				PairedService scpService(sockets[1], classUID);

				dicom::NDeleteSCU scu(scuService,classUID);
				scu.writeRQ(instUID);

				dicom::DataSet request;
				requireRead(scpService,request);
				const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
				dicom::CommandSet::NDeleteRSP responseCommand(
					messageID,
					classUID,
					instUID,
					dicom::Status::SUCCESS);
				mutateResponse(responseCommand);
				scpService.WriteCommand(responseCommand,classUID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(status,response,data);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertEventResponseRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuService(sockets[0], classUID);
				PairedService scpService(sockets[1], classUID);

				dicom::NEventReportSCU scu(scuService,classUID);
				scu.writeRQ(instUID,UINT16(3));

				dicom::DataSet request;
				requireRead(scpService,request);
				const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
				dicom::CommandSet::NEventReportRSP responseCommand(
					messageID,
					classUID,
					instUID,
					dicom::Status::SUCCESS,
					UINT16(3),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateResponse(responseCommand);
				scpService.WriteCommand(responseCommand,classUID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(status,response,data);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertGetResponseRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuService(sockets[0], classUID);
				PairedService scpService(sockets[1], classUID);

				dicom::NGetSCU scu(scuService,classUID);
				scu.writeRQ(instUID,std::vector<dicom::Tag>());

				dicom::DataSet request;
				requireRead(scpService,request);
				const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
				dicom::CommandSet::NGetRSP responseCommand(
					messageID,
					classUID,
					instUID,
					UINT16(0x0110),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateResponse(responseCommand);
				scpService.WriteCommand(responseCommand,classUID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(status,response,data);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertSetResponseRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuService(sockets[0], classUID);
				PairedService scpService(sockets[1], classUID);

				dicom::DataSet requestData;
				requestData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, instUID);
				dicom::NSetSCU scu(scuService,classUID);
				scu.writeRQ(instUID,requestData);

				dicom::DataSet request;
				requireRead(scpService,request);
				dicom::DataSet ignoredRequestData;
				requireRead(scpService,ignoredRequestData);
				const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
				dicom::CommandSet::NSetRSP responseCommand(
					messageID,
					classUID,
					instUID,
					dicom::Status::SUCCESS,
					dicom::DataSetStatus::NO_DATA_SET);
				mutateResponse(responseCommand);
				scpService.WriteCommand(responseCommand,classUID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(status,response,data);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertActionResponseRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuService(sockets[0], classUID);
				PairedService scpService(sockets[1], classUID);

				dicom::NActionSCU scu(scuService,classUID);
				scu.writeRQ(instUID,UINT16(7));

				dicom::DataSet request;
				requireRead(scpService,request);
				const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
				dicom::CommandSet::NActionRSP responseCommand(
					messageID,
					classUID,
					instUID,
					dicom::Status::SUCCESS,
					UINT16(7),
					dicom::DataSetStatus::NO_DATA_SET);
				mutateResponse(responseCommand);
				scpService.WriteCommand(responseCommand,classUID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(status,response,data);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertCreateResponseRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuService(sockets[0], classUID);
				PairedService scpService(sockets[1], classUID);

				dicom::NCreateSCU scu(scuService,classUID);
				scu.writeRQ(instUID);

				dicom::DataSet request;
				requireRead(scpService,request);
				const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
				dicom::CommandSet::NCreateRSP responseCommand(
					messageID,
					classUID,
					instUID,
					dicom::Status::SUCCESS,
					dicom::DataSetStatus::NO_DATA_SET);
				mutateResponse(responseCommand);
				scpService.WriteCommand(responseCommand,classUID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(status,response,data);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertAllNdimseResponsesRejected =
			[&](const std::function<void(dicom::DataSet&)>& mutateResponse)
			{
				assertEventResponseRejected(mutateResponse);
				assertGetResponseRejected(mutateResponse);
				assertSetResponseRejected(mutateResponse);
				assertActionResponseRejected(mutateResponse);
				assertCreateResponseRejected(mutateResponse);
				assertDeleteResponseRejected(mutateResponse);
			};
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_LO>(dicom::TAG_PAT_ID,std::string("PATIENT"));
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(167));
			});

		const std::vector<dicom::Tag> mandatoryResponseFields = {
			dicom::TAG_CMD_FIELD,
			dicom::TAG_MSG_ID_RSP,
			dicom::TAG_DATA_SET_TYPE,
			dicom::TAG_STATUS
		};
		for(std::vector<dicom::Tag>::const_iterator I=mandatoryResponseFields.begin();
			I!=mandatoryResponseFields.end();
			++I)
		{
			const dicom::Tag field = *I;
			const auto removeField =
				[field](dicom::DataSet& response)
				{
					response.erase(field);
			};
			assertAllNdimseResponsesRejected(removeField);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NEventReportSCU scu(scuService,classUID);
			scu.writeRQ(instUID,UINT16(3));

			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				UINT16(3),
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NGetSCU scu(scuService,classUID);
			scu.writeRQ(instUID,std::vector<dicom::Tag>());

			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NGetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				UINT16(0x0110),
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == 0x0110);
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

			dicom::DataSet request;
			requireRead(scpService,request);
			dicom::DataSet ignoredRequestData;
			requireRead(scpService,ignoredRequestData);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NSetRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NActionSCU scu(scuService,classUID);
			scu.writeRQ(instUID,UINT16(7));

			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				UINT16(7),
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NCreateSCU scu(scuService,classUID);
			scu.writeRQ(instUID);

			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NCreateRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NDeleteSCU scu(scuService,classUID);
			scu.writeRQ(instUID);

			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NDeleteRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpService.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
		}

		assertEventResponseRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			});
		assertGetResponseRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			});
		assertSetResponseRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			});
		assertActionResponseRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			});
		assertCreateResponseRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			});
		assertDeleteResponseRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_GET_RSP);
			});

		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_MSG_ID_RSP);
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,UINT16(0));
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_AFF_SOP_CLASS_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::UID("1.2.826.0.1.3680043.10.1553.31.99"));
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_AFF_SOP_INST_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_INST_UID,
					dicom::UID("1.2.826.0.1.3680043.10.1553.31.1.99"));
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.erase(dicom::TAG_STATUS);
				response.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::PENDING);
			});

		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD,dicom::Command::N_DELETE_RSP);
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP,UINT16(1));
			});
		assertAllNdimseResponsesRejected(
			[&](dicom::DataSet& response)
			{
				response.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
			});
		assertAllNdimseResponsesRejected(
			[&](dicom::DataSet& response)
			{
				response.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID,instUID);
			});
		assertAllNdimseResponsesRejected(
			[&](dicom::DataSet& response)
			{
				response.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_CLASS_UID,classUID);
			});
		assertAllNdimseResponsesRejected(
			[&](dicom::DataSet& response)
			{
				response.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID,instUID);
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(
					dicom::TAG_DATA_SET_TYPE,
					dicom::DataSetStatus::NO_DATA_SET);
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_STATUS,dicom::Status::SUCCESS);
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_LO>(
					dicom::TAG_ERR_COMMENT,
					std::string("first"));
				response.Put<dicom::VR_LO>(
					dicom::TAG_ERR_COMMENT,
					std::string("second"));
			});
		assertAllNdimseResponsesRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ERR_ID,UINT16(1));
				response.Put<dicom::VR_US>(dicom::TAG_ERR_ID,UINT16(2));
			});

		assertGetResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
			});
		assertSetResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
			});
		assertActionResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
			});
		assertCreateResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
			});
		assertDeleteResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
			});

		assertEventResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
			});
		assertGetResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
			});
		assertSetResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
			});
		assertCreateResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
			});
		assertDeleteResponseRejected(
			[](dicom::DataSet& response)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
			});

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NEventReportSCU scu(scuService,classUID);
			scu.writeRQ(instUID,3);
			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NEventReportRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				UINT16(3),
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_EVENT_TYPE_ID,UINT16(3));
			scpService.WriteCommand(responseCommand,classUID);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			bool rejected = false;
			try
			{
				scu.readRSP(status,response,data);
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
			PairedService scuService(sockets[0], classUID);
			PairedService scpService(sockets[1], classUID);

			dicom::NActionSCU scu(scuService,classUID);
			scu.writeRQ(instUID,7);
			dicom::DataSet request;
			requireRead(scpService,request);
			const UINT16 messageID = get<UINT16>(request,dicom::TAG_MSG_ID);
			dicom::CommandSet::NActionRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS,
				UINT16(7),
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.Put<dicom::VR_US>(dicom::TAG_ACTION_TYPE_ID,UINT16(7));
			scpService.WriteCommand(responseCommand,classUID);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			bool rejected = false;
			try
			{
				scu.readRSP(status,response,data);
			}
			catch(const std::exception&)
			{
				rejected = true;
			}
			assert(rejected);
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

	void checkCdimseAsynchronousOperationsWindowEnforcement()
	{
		const dicom::UID classUID("1.2.840.10008.5.1.4.1.2.2.1");

		{
			NullService service;
			service.MaximumNumberOperationsInvoked_ = 0;
			service.BeginInvokedOperation(101);
			assert(service.OutstandingOperationsInvoked() == 1);
			assert(service.IsInvokedOperationOutstanding(101));

			bool duplicateRejected = false;
			try
			{
				service.BeginInvokedOperation(101);
			}
			catch(const std::exception&)
			{
				duplicateRejected = true;
			}
			assert(duplicateRejected);
			assert(service.OutstandingOperationsInvoked() == 1);

			service.BeginInvokedOperation(103);
			assert(service.OutstandingOperationsInvoked() == 2);
			service.CompleteInvokedOperation(101);
			assert(!service.IsInvokedOperationOutstanding(101));
			assert(service.IsInvokedOperationOutstanding(103));
			assert(service.OutstandingOperationsInvoked() == 1);
			service.CompleteInvokedOperation(101);
			assert(service.OutstandingOperationsInvoked() == 1);
			service.CompleteInvokedOperation(103);
			assert(service.OutstandingOperationsInvoked() == 0);

			service.BeginInvokedOperation(105);
			service.BeginPerformedOperation(205);
			service.RequestCancel(305);
			assert(service.OutstandingOperationsInvoked() == 1);
			assert(service.OutstandingOperationsPerformed() == 1);
			assert(service.HasCancelRequest());
			service.ClearNegotiatedAssociationOptions();
			assert(service.OutstandingOperationsInvoked() == 0);
			assert(service.OutstandingOperationsPerformed() == 0);
			assert(!service.IsInvokedOperationOutstanding(105));
			assert(!service.IsPerformedOperationOutstanding(205));
			assert(!service.HasCancelRequest());
			assert(!service.IsCancelRequested(305));
		}

		{
			NullService service;
			service.MaximumNumberOperationsPerformed_ = 0;
			service.BeginPerformedOperation(201);
			assert(service.OutstandingOperationsPerformed() == 1);
			assert(service.IsPerformedOperationOutstanding(201));

			bool duplicateRejected = false;
			try
			{
				service.BeginPerformedOperation(201);
			}
			catch(const std::exception&)
			{
				duplicateRejected = true;
			}
			assert(duplicateRejected);
			assert(service.OutstandingOperationsPerformed() == 1);

			service.BeginPerformedOperation(203);
			assert(service.OutstandingOperationsPerformed() == 2);
			service.CompletePerformedOperation(201);
			assert(!service.IsPerformedOperationOutstanding(201));
			assert(service.IsPerformedOperationOutstanding(203));
			assert(service.OutstandingOperationsPerformed() == 1);
			service.CompletePerformedOperation(203);
			assert(service.OutstandingOperationsPerformed() == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CFindSCU scu(scuSide, classUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			scu.writeRQ(query);
			assert(scuSide.OutstandingOperationsInvoked() == 1);
			assert(!scuSide.CanInvokeOperation());

			bool secondRequestRejected = false;
			try
			{
				scu.writeRQ(query);
			}
			catch(const std::exception&)
			{
				secondRequestRejected = true;
			}
			assert(secondRequestRejected);
			assert(scuSide.OutstandingOperationsInvoked() == 1);

			dicom::DataSet requestCommand;
			dicom::DataSet requestData;
			requireRead(scpSide,requestCommand);
			requireRead(scpSide,requestData);
			const UINT16 messageID = get<UINT16>(requestCommand,dicom::TAG_MSG_ID);
			dicom::CommandSet::CFindRSP finalResponse(
				messageID,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(finalResponse,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(scuSide.OutstandingOperationsInvoked() == 0);
			assert(scuSide.CanInvokeOperation());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CFindSCU scu(scuSide, classUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			scu.writeRQ(query);

			dicom::DataSet requestCommand;
			dicom::DataSet requestData;
			requireRead(scpSide,requestCommand);
			requireRead(scpSide,requestData);
			const UINT16 messageID = get<UINT16>(requestCommand,dicom::TAG_MSG_ID);

			dicom::CommandSet::CFindRSP pendingResponse(
				messageID,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet match;
			match.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			scpSide.WriteCommand(pendingResponse,classUID);
			scpSide.WriteDataSet(match,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::PENDING);
			assert(scuSide.OutstandingOperationsInvoked() == 1);
			assert(!scuSide.CanInvokeOperation());

			bool secondRequestRejected = false;
			try
			{
				scu.writeRQ(query);
			}
			catch(const std::exception&)
			{
				secondRequestRejected = true;
			}
			assert(secondRequestRejected);

			dicom::CommandSet::CFindRSP finalResponse(
				messageID,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(finalResponse,classUID);
			data = dicom::DataSet();
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(scuSide.OutstandingOperationsInvoked() == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			scuSide.MaximumNumberOperationsInvoked_ = 0;

			dicom::CFindSCU firstSCU(scuSide, classUID);
			dicom::CFindSCU secondSCU(scuSide, classUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			firstSCU.writeRQ(query);
			secondSCU.writeRQ(query);
			assert(scuSide.OutstandingOperationsInvoked() == 2);
			assert(scuSide.CanInvokeOperation());

			bool sameSCURejected = false;
			try
			{
				firstSCU.writeRQ(query);
			}
			catch(const std::exception&)
			{
				sameSCURejected = true;
			}
			assert(sameSCURejected);
			assert(scuSide.OutstandingOperationsInvoked() == 2);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CFindRQ request(43,classUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			requestorSide.WriteDataSet(query,classUID);

			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			bool handlerObservedPerformed = false;
			dicom::HandleCFind(
				dicom::CFindStatusFunction(
					[&](dicom::ServiceBase& service, dicom::DataSet&, dicom::Sequence&)
					{
						assert(service.OutstandingOperationsPerformed() == 1);
						assert(!service.CanPerformOperation());
						handlerObservedPerformed = true;
						return dicom::Status::SUCCESS;
					}),
				scpSide,
				readRequest,
				classUID);
			assert(handlerObservedPerformed);
			assert(scpSide.OutstandingOperationsPerformed() == 0);
			assert(scpSide.CanPerformOperation());
		}

		{
			NullService service;
			service.BeginPerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 1);
			assert(!service.CanPerformOperation());

			dicom::CommandSet::CEchoRQ request(45,classUID);
			bool rejected = false;
			try
			{
				dicom::HandleCEcho(service,request,classUID);
			}
			catch(const std::exception&)
			{
				rejected = true;
			}
			assert(rejected);
			assert(service.OutstandingOperationsPerformed() == 1);
			service.CompletePerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 0);
		}

		{
			NullService service;
			service.MaximumNumberOperationsPerformed_ = 0;
			service.BeginPerformedOperation();
			service.BeginPerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 2);
			assert(service.CanPerformOperation());
			service.CompletePerformedOperation();
			service.CompletePerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 0);
		}
	}

	void checkNdimseAsynchronousOperationsWindowEnforcement()
	{
		const dicom::UID classUID("1.2.826.0.1.3680043.10.1553.30");
		const dicom::UID instUID("1.2.826.0.1.3680043.10.1553.30.1");

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::NDeleteSCU scu(scuSide,classUID);
			scu.writeRQ(instUID);
			assert(scuSide.OutstandingOperationsInvoked() == 1);
			assert(!scuSide.CanInvokeOperation());

			bool secondRequestRejected = false;
			try
			{
				scu.writeRQ(instUID);
			}
			catch(const std::exception&)
			{
				secondRequestRejected = true;
			}
			assert(secondRequestRejected);
			assert(scuSide.OutstandingOperationsInvoked() == 1);

			dicom::DataSet requestCommand;
			requireRead(scpSide,requestCommand);
			const UINT16 messageID = get<UINT16>(requestCommand,dicom::TAG_MSG_ID);
			dicom::CommandSet::NDeleteRSP responseCommand(
				messageID,
				classUID,
				instUID,
				dicom::Status::SUCCESS);
			scpSide.WriteCommand(responseCommand,classUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::SUCCESS);
			assert(scuSide.OutstandingOperationsInvoked() == 0);
			assert(scuSide.CanInvokeOperation());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			scuSide.MaximumNumberOperationsInvoked_ = 0;

			dicom::NDeleteSCU firstSCU(scuSide,classUID);
			dicom::NDeleteSCU secondSCU(scuSide,classUID);
			firstSCU.writeRQ(instUID);
			secondSCU.writeRQ(instUID);
			assert(scuSide.OutstandingOperationsInvoked() == 2);
			assert(scuSide.CanInvokeOperation());

			bool sameSCURejected = false;
			try
			{
				firstSCU.writeRQ(instUID);
			}
			catch(const std::exception&)
			{
				sameSCURejected = true;
			}
			assert(sameSCURejected);
			assert(scuSide.OutstandingOperationsInvoked() == 2);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService service(sockets[0], classUID);
			dicom::CommandSet::NDeleteRQ request(51,classUID,instUID);
			bool handlerObservedPerformed = false;
			dicom::HandleNDelete(
				[&handlerObservedPerformed](
					dicom::ServiceBase& pdu,
					const dicom::DataSet&,
					const dicom::DataSet&,
					dicom::DataSet&)
				{
					assert(pdu.OutstandingOperationsPerformed() == 1);
					assert(!pdu.CanPerformOperation());
					handlerObservedPerformed = true;
					return dicom::Status::SUCCESS;
				},
				service,
				request,
				classUID);
			assert(handlerObservedPerformed);
			assert(service.OutstandingOperationsPerformed() == 0);
			assert(service.CanPerformOperation());
		}

		{
			NullService service;
			service.BeginPerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 1);
			assert(!service.CanPerformOperation());

			dicom::CommandSet::NDeleteRQ request(53,classUID,instUID);
			bool rejected = false;
			try
			{
				dicom::HandleNDelete(
					[](dicom::ServiceBase&, const dicom::DataSet&, const dicom::DataSet&, dicom::DataSet&)
					{
						return dicom::Status::SUCCESS;
					},
					service,
					request,
					classUID);
			}
			catch(const std::exception&)
			{
				rejected = true;
			}
			assert(rejected);
			assert(service.OutstandingOperationsPerformed() == 1);
			service.CompletePerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 0);
		}

		{
			NullService service;
			service.MaximumNumberOperationsPerformed_ = 0;
			service.BeginPerformedOperation();
			service.BeginPerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 2);
			assert(service.CanPerformOperation());
			service.CompletePerformedOperation();
			service.CompletePerformedOperation();
			assert(service.OutstandingOperationsPerformed() == 0);
		}
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
		assert(service.HasCancelRequest());
		assert(service.IsCancelRequested(7));
		assert(dicom::PollCCancelRQ(service));
		service.ClearCancelRequest(7);
		assert(!service.HasCancelRequest());
		assert(!service.IsCancelRequested(7));
		assert(!dicom::PollCCancelRQ(service));

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

		dicom::CommandSet::CCancelRQ missingCommandField(6);
		missingCommandField.erase(dicom::TAG_CMD_FIELD);
		bool missingCommandFieldRejected = false;
		try
		{
			dicom::HandleCCancel(service, missingCommandField);
		}
		catch(const std::exception&)
		{
			missingCommandFieldRejected = true;
		}
		assert(missingCommandFieldRejected);

		dicom::CommandSet::CCancelRQ missingMessageID(10);
		missingMessageID.erase(dicom::TAG_MSG_ID_RSP);
		bool missingMessageIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, missingMessageID);
		}
		catch(const std::exception&)
		{
			missingMessageIDRejected = true;
		}
		assert(missingMessageIDRejected);

		dicom::CommandSet::CCancelRQ missingDataSetType(12);
		missingDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		bool missingDataSetTypeRejected = false;
		try
		{
			dicom::HandleCCancel(service, missingDataSetType);
		}
		catch(const std::exception&)
		{
			missingDataSetTypeRejected = true;
		}
		assert(missingDataSetTypeRejected);

		dicom::CommandSet::CCancelRQ duplicateCommandField(8);
		duplicateCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_CANCEL_RQ);
		bool duplicateCommandFieldRejected = false;
		try
		{
			dicom::HandleCCancel(service, duplicateCommandField);
		}
		catch(const std::exception&)
		{
			duplicateCommandFieldRejected = true;
		}
		assert(duplicateCommandFieldRejected);

		dicom::CommandSet::CCancelRQ duplicateMessageID(9);
		duplicateMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(9));
		bool duplicateMessageIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, duplicateMessageID);
		}
		catch(const std::exception&)
		{
			duplicateMessageIDRejected = true;
		}
		assert(duplicateMessageIDRejected);

		dicom::CommandSet::CCancelRQ unexpectedMessageID(13);
		unexpectedMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(13));
		bool unexpectedMessageIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, unexpectedMessageID);
		}
		catch(const std::exception&)
		{
			unexpectedMessageIDRejected = true;
		}
		assert(unexpectedMessageIDRejected);

		dicom::CommandSet::CCancelRQ duplicateDataSetType(11);
		duplicateDataSetType.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::NO_DATA_SET);
		bool duplicateDataSetTypeRejected = false;
		try
		{
			dicom::HandleCCancel(service, duplicateDataSetType);
		}
		catch(const std::exception&)
		{
			duplicateDataSetTypeRejected = true;
		}
		assert(duplicateDataSetTypeRejected);

		dicom::CommandSet::CCancelRQ requestedClassUID(14);
		requestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			dicom::VERIFICATION_SOP_CLASS);
		bool requestedClassUIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, requestedClassUID);
		}
		catch(const std::exception&)
		{
			requestedClassUIDRejected = true;
		}
		assert(requestedClassUIDRejected);

		dicom::CommandSet::CCancelRQ requestedInstanceUID(16);
		requestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.11.16"));
		bool requestedInstanceUIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, requestedInstanceUID);
		}
		catch(const std::exception&)
		{
			requestedInstanceUIDRejected = true;
		}
		assert(requestedInstanceUIDRejected);

		dicom::CommandSet::CCancelRQ affectedClassUID(18);
		affectedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_CLASS_UID,
			dicom::VERIFICATION_SOP_CLASS);
		bool affectedClassUIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, affectedClassUID);
		}
		catch(const std::exception&)
		{
			affectedClassUIDRejected = true;
		}
		assert(affectedClassUIDRejected);

		dicom::CommandSet::CCancelRQ affectedInstanceUID(20);
		affectedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.11.20"));
		bool affectedInstanceUIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, affectedInstanceUID);
		}
		catch(const std::exception&)
		{
			affectedInstanceUIDRejected = true;
		}
		assert(affectedInstanceUIDRejected);

		dicom::CommandSet::CCancelRQ priority(22);
		priority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		bool priorityRejected = false;
		try
		{
			dicom::HandleCCancel(service, priority);
		}
		catch(const std::exception&)
		{
			priorityRejected = true;
		}
		assert(priorityRejected);

		dicom::CommandSet::CCancelRQ moveDestination(24);
		moveDestination.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		bool moveDestinationRejected = false;
		try
		{
			dicom::HandleCCancel(service, moveDestination);
		}
		catch(const std::exception&)
		{
			moveDestinationRejected = true;
		}
		assert(moveDestinationRejected);

		dicom::CommandSet::CCancelRQ moveOriginatorAET(26);
		moveOriginatorAET.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
		bool moveOriginatorAETRejected = false;
		try
		{
			dicom::HandleCCancel(service, moveOriginatorAET);
		}
		catch(const std::exception&)
		{
			moveOriginatorAETRejected = true;
		}
		assert(moveOriginatorAETRejected);

		dicom::CommandSet::CCancelRQ moveOriginatorMessageID(28);
		moveOriginatorMessageID.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(28));
		bool moveOriginatorMessageIDRejected = false;
		try
		{
			dicom::HandleCCancel(service, moveOriginatorMessageID);
		}
		catch(const std::exception&)
		{
			moveOriginatorMessageIDRejected = true;
		}
		assert(moveOriginatorMessageIDRejected);
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
		const auto addRetrieveCounters =
			[](dicom::DataSet& response, bool remaining, bool completed, bool failed, bool warning)
			{
				if(remaining)
					response.Put<dicom::VR_US>(dicom::TAG_NUM_REMAIN_SUBOP, UINT16(3));
				if(completed)
					response.Put<dicom::VR_US>(dicom::TAG_NUM_COMPL_SUBOP, UINT16(2));
				if(failed)
					response.Put<dicom::VR_US>(dicom::TAG_NUM_FAIL_SUBOP, UINT16(1));
				if(warning)
					response.Put<dicom::VR_US>(dicom::TAG_NUM_WARN_SUBOP, UINT16(0));
			};
		const auto assertCGetPendingCountersRejected =
			[&](bool remaining, bool completed, bool failed, bool warning, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);

				dicom::CommandSet::CGetRSP responseCommand(
					messageID,
					classUID,
					dicom::Status::PENDING,
					dicom::DataSetStatus::NO_DATA_SET);
				addRetrieveCounters(responseCommand, remaining, completed, failed, warning);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCGetSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

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
			};
		const auto assertCGetPendingCountersRejectedWithStoreHandler =
			[&](bool remaining, bool completed, bool failed, bool warning, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);

				dicom::CommandSet::CGetRSP responseCommand(
					messageID,
					classUID,
					dicom::Status::PENDING,
					dicom::DataSetStatus::NO_DATA_SET);
				addRetrieveCounters(responseCommand, remaining, completed, failed, warning);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCGetSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(
						status,
						response,
						data,
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
						});
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertCMovePendingCountersRejected =
			[&](bool remaining, bool completed, bool failed, bool warning, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);

				dicom::CommandSet::CMoveRSP responseCommand(
					messageID,
					classUID,
					dicom::Status::PENDING,
					dicom::DataSetStatus::NO_DATA_SET);
				addRetrieveCounters(responseCommand, remaining, completed, failed, warning);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCMoveSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

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
			};
		const auto assertCGetResponseRejected =
			[&](dicom::DataSet responseCommand, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCGetSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

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
			};
		const auto assertCGetResponseRejectedWithStoreHandler =
			[&](dicom::DataSet responseCommand, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCGetSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				bool rejected = false;
				try
				{
					scu.readRSP(
						status,
						response,
						data,
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
						});
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertCMoveResponseRejected =
			[&](dicom::DataSet responseCommand, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCMoveSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

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
			};
		const auto assertCFindResponseRejected =
			[&](dicom::DataSet responseCommand, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCFindSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

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
			};
		const auto assertCFindResponseAcceptedStatus =
			[&](UINT16 acceptedStatus, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				dicom::CommandSet::CFindRSP responseCommand(
					messageID,
					classUID,
					acceptedStatus,
					dicom::DataSetStatus::NO_DATA_SET);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCFindSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				scu.readRSP(status, response, data);
				assert(status == acceptedStatus);
				assert(data.empty());
			};
		const auto assertCGetResponseAcceptedStatus =
			[&](UINT16 acceptedStatus, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				dicom::CommandSet::CGetRSP responseCommand(
					messageID,
					classUID,
					acceptedStatus,
					dicom::DataSetStatus::NO_DATA_SET);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCGetSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				scu.readRSP(status, response, data);
				assert(status == acceptedStatus);
				assert(data.empty());
			};
		const auto assertCMoveResponseAcceptedStatus =
			[&](UINT16 acceptedStatus, UINT16 messageID)
			{
				int sockets[2];
				makeSocketPair(sockets);
				PairedService scuSide(sockets[0], classUID);
				PairedService scpSide(sockets[1], classUID);
				dicom::CommandSet::CMoveRSP responseCommand(
					messageID,
					classUID,
					acceptedStatus,
					dicom::DataSetStatus::NO_DATA_SET);
				scpSide.WriteCommand(responseCommand, classUID);

				TestCMoveSCU scu(scuSide, classUID);
				scu.setLastMessageID(messageID);

				UINT16 status = 0;
				dicom::DataSet response;
				dicom::DataSet data;
				scu.readRSP(status, response, data);
				assert(status == acceptedStatus);
				assert(data.empty());
			};

		assertCFindResponseAcceptedStatus(0xa700, 233);
		assertCFindResponseAcceptedStatus(0xc123, 235);
		assertCGetResponseAcceptedStatus(0xa701, 237);
		assertCGetResponseAcceptedStatus(0xc123, 239);
		assertCMoveResponseAcceptedStatus(0xa801, 241);
		assertCMoveResponseAcceptedStatus(0xc123, 243);
		assertCFindResponseAcceptedStatus(0x0122, 245);
		assertCGetResponseAcceptedStatus(0x0122, 247);
		assertCGetResponseAcceptedStatus(0x0124, 249);
		assertCGetResponseAcceptedStatus(0x0210, 251);
		assertCGetResponseAcceptedStatus(0x0211, 252);
		assertCGetResponseAcceptedStatus(0x0212, 254);
		assertCMoveResponseAcceptedStatus(0x0122, 256);
		assertCMoveResponseAcceptedStatus(0x0124, 258);
		assertCMoveResponseAcceptedStatus(0x0210, 260);
		assertCMoveResponseAcceptedStatus(0x0211, 262);
		assertCMoveResponseAcceptedStatus(0x0212, 264);

		dicom::CommandSet::CFindRSP findResponseWithDataElement(
			266,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithDataElement.Put<dicom::VR_LO>(
			dicom::TAG_PAT_ID,
			std::string("PATIENT"));
		assertCFindResponseRejected(findResponseWithDataElement, 266);

		dicom::CommandSet::CFindRSP findResponseWithMessageID(
			268,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(268));
		assertCFindResponseRejected(findResponseWithMessageID, 268);

		dicom::CommandSet::CFindRSP findResponseWithRetrieveCounter(
			253,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithRetrieveCounter.Put<dicom::VR_US>(
			dicom::TAG_NUM_REMAIN_SUBOP,
			UINT16(0));
		assertCFindResponseRejected(findResponseWithRetrieveCounter, 253);

		dicom::CommandSet::CFindRSP findResponseWithSOPInstanceUID(
			255,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithSOPInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.14.24"));
		assertCFindResponseRejected(findResponseWithSOPInstanceUID, 255);

		dicom::CommandSet::CGetRSP getResponseWithSOPInstanceUID(
			257,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		getResponseWithSOPInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.14.25"));
		assertCGetResponseRejected(getResponseWithSOPInstanceUID, 257);

		dicom::CommandSet::CMoveRSP moveResponseWithSOPInstanceUID(
			259,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		moveResponseWithSOPInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.14.26"));
		assertCMoveResponseRejected(moveResponseWithSOPInstanceUID, 259);

		dicom::CommandSet::CFindRSP findResponseWithRequestedClassUID(
			261,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			classUID);
		assertCFindResponseRejected(findResponseWithRequestedClassUID, 261);

		dicom::CommandSet::CGetRSP getResponseWithRequestedClassUID(
			263,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		getResponseWithRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			classUID);
		assertCGetResponseRejected(getResponseWithRequestedClassUID, 263);

		dicom::CommandSet::CMoveRSP moveResponseWithRequestedClassUID(
			265,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		moveResponseWithRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			classUID);
		assertCMoveResponseRejected(moveResponseWithRequestedClassUID, 265);

		dicom::CommandSet::CFindRSP findResponseWithRequestedInstanceUID(
			267,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.14.27"));
		assertCFindResponseRejected(findResponseWithRequestedInstanceUID, 267);

		dicom::CommandSet::CGetRSP getResponseWithRequestedInstanceUID(
			269,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		getResponseWithRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.14.28"));
		assertCGetResponseRejected(getResponseWithRequestedInstanceUID, 269);

		dicom::CommandSet::CMoveRSP moveResponseWithRequestedInstanceUID(
			271,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		moveResponseWithRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.14.29"));
		assertCMoveResponseRejected(moveResponseWithRequestedInstanceUID, 271);

		dicom::CommandSet::CFindRSP findResponseWithPriority(
			273,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		assertCFindResponseRejected(findResponseWithPriority, 273);

		dicom::CommandSet::CGetRSP getResponseWithMoveDestination(
			275,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		getResponseWithMoveDestination.Put<dicom::VR_AE>(
			dicom::TAG_MOVE_DEST,
			std::string("ARCHIVE_AE"));
		assertCGetResponseRejected(getResponseWithMoveDestination, 275);

		dicom::CommandSet::CMoveRSP moveResponseWithMoveOriginatorAET(
			277,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		moveResponseWithMoveOriginatorAET.Put<dicom::VR_AE>(
			dicom::TAG_MOVE_ORIG_AET,
			std::string("MOVE_AE"));
		assertCMoveResponseRejected(moveResponseWithMoveOriginatorAET, 277);

		dicom::CommandSet::CFindRSP findResponseWithMoveDestination(
			279,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		findResponseWithMoveDestination.Put<dicom::VR_AE>(
			dicom::TAG_MOVE_DEST,
			std::string("ARCHIVE_AE"));
		assertCFindResponseRejected(findResponseWithMoveDestination, 279);

		dicom::CommandSet::CGetRSP getResponseWithMoveOriginatorMessageID(
			281,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		getResponseWithMoveOriginatorMessageID.Put<dicom::VR_US>(
			dicom::TAG_MOVE_ORIG_MSG_ID,
			UINT16(281));
		assertCGetResponseRejected(getResponseWithMoveOriginatorMessageID, 281);

		dicom::CommandSet::CMoveRSP moveResponseWithPriority(
			283,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		moveResponseWithPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		assertCMoveResponseRejected(moveResponseWithPriority, 283);

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CFindRSP responseCommand(
				245,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpSide.WriteCommand(responseCommand, classUID);

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(245);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::SUCCESS);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CGetRSP responseCommand(
				247,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpSide.WriteCommand(responseCommand, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(247);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::SUCCESS);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CMoveRSP responseCommand(
				249,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpSide.WriteCommand(responseCommand, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(249);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::SUCCESS);
			assert(data.empty());
		}

		dicom::CommandSet::CFindRSP duplicateResponseCommandField(
			49,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_FIND_RSP);
		assertCFindResponseRejected(duplicateResponseCommandField, 49);

		dicom::CommandSet::CFindRSP duplicateResponseMessageID(
			51,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(51));
		assertCFindResponseRejected(duplicateResponseMessageID, 51);

		dicom::CommandSet::CFindRSP duplicateResponseSOPClassUID(
			53,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		assertCFindResponseRejected(duplicateResponseSOPClassUID, 53);

		dicom::CommandSet::CFindRSP duplicateResponseStatus(
			55,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseStatus.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::SUCCESS);
		assertCFindResponseRejected(duplicateResponseStatus, 55);

		dicom::CommandSet::CFindRSP duplicateResponseDataSetType(
			57,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::NO_DATA_SET);
		assertCFindResponseRejected(duplicateResponseDataSetType, 57);

		dicom::CommandSet::CFindRSP duplicateResponseErrorComment(
			59,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseErrorComment.Put<dicom::VR_LO>(
			dicom::TAG_ERR_COMMENT,
			std::string("first"));
		duplicateResponseErrorComment.Put<dicom::VR_LO>(
			dicom::TAG_ERR_COMMENT,
			std::string("second"));
		assertCFindResponseRejected(duplicateResponseErrorComment, 59);

		dicom::CommandSet::CFindRSP duplicateResponseErrorID(
			61,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateResponseErrorID.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(1));
		duplicateResponseErrorID.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(2));
		assertCFindResponseRejected(duplicateResponseErrorID, 61);

		dicom::CommandSet::CFindRSP missingResponseCommandField(
			195,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingResponseCommandField.erase(dicom::TAG_CMD_FIELD);
		assertCFindResponseRejected(missingResponseCommandField, 195);

		dicom::CommandSet::CFindRSP missingResponseMessageID(
			197,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingResponseMessageID.erase(dicom::TAG_MSG_ID_RSP);
		assertCFindResponseRejected(missingResponseMessageID, 197);

		dicom::CommandSet::CFindRSP missingResponseStatus(
			199,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingResponseStatus.erase(dicom::TAG_STATUS);
		assertCFindResponseRejected(missingResponseStatus, 199);

		dicom::CommandSet::CFindRSP missingResponseDataSetType(
			201,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingResponseDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertCFindResponseRejected(missingResponseDataSetType, 201);

		dicom::CommandSet::CFindRSP wrongResponseCommandField(
			219,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		wrongResponseCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongResponseCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RSP);
		assertCFindResponseRejected(wrongResponseCommandField, 219);

		dicom::CommandSet::CGetRSP duplicateGetResponseCommandField(
			163,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_GET_RSP);
		assertCGetResponseRejected(duplicateGetResponseCommandField, 163);

		dicom::CommandSet::CGetRSP duplicateGetResponseMessageID(
			165,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(165));
		assertCGetResponseRejectedWithStoreHandler(duplicateGetResponseMessageID, 165);

		dicom::CommandSet::CGetRSP duplicateGetResponseSOPClassUID(
			167,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		assertCGetResponseRejected(duplicateGetResponseSOPClassUID, 167);

		dicom::CommandSet::CGetRSP duplicateGetResponseStatus(
			169,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseStatus.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::SUCCESS);
		assertCGetResponseRejectedWithStoreHandler(duplicateGetResponseStatus, 169);

		dicom::CommandSet::CGetRSP duplicateGetResponseDataSetType(
			171,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::NO_DATA_SET);
		assertCGetResponseRejected(duplicateGetResponseDataSetType, 171);

		dicom::CommandSet::CGetRSP duplicateGetResponseErrorComment(
			173,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseErrorComment.Put<dicom::VR_LO>(
			dicom::TAG_ERR_COMMENT,
			std::string("first"));
		duplicateGetResponseErrorComment.Put<dicom::VR_LO>(
			dicom::TAG_ERR_COMMENT,
			std::string("second"));
		assertCGetResponseRejectedWithStoreHandler(duplicateGetResponseErrorComment, 173);

		dicom::CommandSet::CGetRSP duplicateGetResponseErrorID(
			175,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateGetResponseErrorID.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(1));
		duplicateGetResponseErrorID.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(2));
		assertCGetResponseRejected(duplicateGetResponseErrorID, 175);

		dicom::CommandSet::CGetRSP invalidGetResponseStatus(
			191,
			classUID,
			UINT16(0x0210),
			dicom::DataSetStatus::NO_DATA_SET);
		assertCGetResponseRejected(invalidGetResponseStatus, 191);

		dicom::CommandSet::CGetRSP missingGetResponseCommandField(
			203,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingGetResponseCommandField.erase(dicom::TAG_CMD_FIELD);
		assertCGetResponseRejected(missingGetResponseCommandField, 203);

		dicom::CommandSet::CGetRSP missingGetResponseMessageID(
			205,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingGetResponseMessageID.erase(dicom::TAG_MSG_ID_RSP);
		assertCGetResponseRejectedWithStoreHandler(missingGetResponseMessageID, 205);

		dicom::CommandSet::CGetRSP missingGetResponseStatus(
			207,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingGetResponseStatus.erase(dicom::TAG_STATUS);
		assertCGetResponseRejected(missingGetResponseStatus, 207);

		dicom::CommandSet::CGetRSP missingGetResponseDataSetType(
			209,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingGetResponseDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertCGetResponseRejectedWithStoreHandler(missingGetResponseDataSetType, 209);

		dicom::CommandSet::CGetRSP wrongGetResponseCommandField(
			221,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		wrongGetResponseCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongGetResponseCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_FIND_RSP);
		assertCGetResponseRejected(wrongGetResponseCommandField, 221);

		dicom::CommandSet::CGetRSP wrongGetResponseMessageID(
			223,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		wrongGetResponseMessageID.erase(dicom::TAG_MSG_ID_RSP);
		wrongGetResponseMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(225));
		assertCGetResponseRejectedWithStoreHandler(wrongGetResponseMessageID, 223);

		dicom::CommandSet::CGetRSP wrongGetResponseSOPClassUID(
			225,
			wrongClassUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		assertCGetResponseRejected(wrongGetResponseSOPClassUID, 225);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseCommandField(
			177,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RSP);
		assertCMoveResponseRejected(duplicateMoveResponseCommandField, 177);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseMessageID(
			179,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(179));
		assertCMoveResponseRejected(duplicateMoveResponseMessageID, 179);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseSOPClassUID(
			181,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		assertCMoveResponseRejected(duplicateMoveResponseSOPClassUID, 181);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseStatus(
			183,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseStatus.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::SUCCESS);
		assertCMoveResponseRejected(duplicateMoveResponseStatus, 183);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseDataSetType(
			185,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::NO_DATA_SET);
		assertCMoveResponseRejected(duplicateMoveResponseDataSetType, 185);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseErrorComment(
			187,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseErrorComment.Put<dicom::VR_LO>(
			dicom::TAG_ERR_COMMENT,
			std::string("first"));
		duplicateMoveResponseErrorComment.Put<dicom::VR_LO>(
			dicom::TAG_ERR_COMMENT,
			std::string("second"));
		assertCMoveResponseRejected(duplicateMoveResponseErrorComment, 187);

		dicom::CommandSet::CMoveRSP duplicateMoveResponseErrorID(
			189,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		duplicateMoveResponseErrorID.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(1));
		duplicateMoveResponseErrorID.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(2));
		assertCMoveResponseRejected(duplicateMoveResponseErrorID, 189);

		dicom::CommandSet::CMoveRSP invalidMoveResponseStatus(
			193,
			classUID,
			UINT16(0x020f),
			dicom::DataSetStatus::NO_DATA_SET);
		assertCMoveResponseRejected(invalidMoveResponseStatus, 193);

		dicom::CommandSet::CMoveRSP missingMoveResponseCommandField(
			211,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingMoveResponseCommandField.erase(dicom::TAG_CMD_FIELD);
		assertCMoveResponseRejected(missingMoveResponseCommandField, 211);

		dicom::CommandSet::CMoveRSP missingMoveResponseMessageID(
			213,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingMoveResponseMessageID.erase(dicom::TAG_MSG_ID_RSP);
		assertCMoveResponseRejected(missingMoveResponseMessageID, 213);

		dicom::CommandSet::CMoveRSP missingMoveResponseStatus(
			215,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingMoveResponseStatus.erase(dicom::TAG_STATUS);
		assertCMoveResponseRejected(missingMoveResponseStatus, 215);

		dicom::CommandSet::CMoveRSP missingMoveResponseDataSetType(
			217,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		missingMoveResponseDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertCMoveResponseRejected(missingMoveResponseDataSetType, 217);

		dicom::CommandSet::CMoveRSP wrongMoveResponseCommandField(
			227,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		wrongMoveResponseCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongMoveResponseCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_GET_RSP);
		assertCMoveResponseRejected(wrongMoveResponseCommandField, 227);

		dicom::CommandSet::CMoveRSP wrongMoveResponseMessageID(
			229,
			classUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		wrongMoveResponseMessageID.erase(dicom::TAG_MSG_ID_RSP);
		wrongMoveResponseMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(231));
		assertCMoveResponseRejected(wrongMoveResponseMessageID, 229);

		dicom::CommandSet::CMoveRSP wrongMoveResponseSOPClassUID(
			231,
			wrongClassUID,
			dicom::Status::SUCCESS,
			dicom::DataSetStatus::NO_DATA_SET);
		assertCMoveResponseRejected(wrongMoveResponseSOPClassUID, 231);

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

		assertCGetPendingCountersRejected(false,true,true,true,49);
		assertCGetPendingCountersRejected(true,false,true,true,51);
		assertCGetPendingCountersRejected(true,true,false,true,53);
		assertCGetPendingCountersRejected(true,true,true,false,55);
		assertCGetPendingCountersRejectedWithStoreHandler(false,true,true,true,57);
		assertCGetPendingCountersRejectedWithStoreHandler(true,false,true,true,59);
		assertCGetPendingCountersRejectedWithStoreHandler(true,true,false,true,61);
		assertCGetPendingCountersRejectedWithStoreHandler(true,true,true,false,63);
		assertCMovePendingCountersRejected(false,true,true,true,65);
		assertCMovePendingCountersRejected(true,false,true,true,67);
		assertCMovePendingCountersRejected(true,true,false,true,69);
		assertCMovePendingCountersRejected(true,true,true,false,71);

		dicom::CommandSet::CGetRSP duplicateGetRemaining(
			73,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateGetRemaining, true, true, true, true);
		duplicateGetRemaining.Put<dicom::VR_US>(dicom::TAG_NUM_REMAIN_SUBOP, UINT16(3));
		assertCGetResponseRejected(duplicateGetRemaining, 73);

		dicom::CommandSet::CGetRSP duplicateGetCompleted(
			75,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateGetCompleted, true, true, true, true);
		duplicateGetCompleted.Put<dicom::VR_US>(dicom::TAG_NUM_COMPL_SUBOP, UINT16(2));
		assertCGetResponseRejectedWithStoreHandler(duplicateGetCompleted, 75);

		dicom::CommandSet::CGetRSP duplicateGetFailed(
			77,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateGetFailed, true, true, true, true);
		duplicateGetFailed.Put<dicom::VR_US>(dicom::TAG_NUM_FAIL_SUBOP, UINT16(1));
		assertCGetResponseRejected(duplicateGetFailed, 77);

		dicom::CommandSet::CGetRSP duplicateGetWarning(
			79,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateGetWarning, true, true, true, true);
		duplicateGetWarning.Put<dicom::VR_US>(dicom::TAG_NUM_WARN_SUBOP, UINT16(0));
		assertCGetResponseRejectedWithStoreHandler(duplicateGetWarning, 79);

		dicom::CommandSet::CMoveRSP duplicateMoveRemaining(
			81,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateMoveRemaining, true, true, true, true);
		duplicateMoveRemaining.Put<dicom::VR_US>(dicom::TAG_NUM_REMAIN_SUBOP, UINT16(3));
		assertCMoveResponseRejected(duplicateMoveRemaining, 81);

		dicom::CommandSet::CMoveRSP duplicateMoveCompleted(
			83,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateMoveCompleted, true, true, true, true);
		duplicateMoveCompleted.Put<dicom::VR_US>(dicom::TAG_NUM_COMPL_SUBOP, UINT16(2));
		assertCMoveResponseRejected(duplicateMoveCompleted, 83);

		dicom::CommandSet::CMoveRSP duplicateMoveFailed(
			85,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateMoveFailed, true, true, true, true);
		duplicateMoveFailed.Put<dicom::VR_US>(dicom::TAG_NUM_FAIL_SUBOP, UINT16(1));
		assertCMoveResponseRejected(duplicateMoveFailed, 85);

		dicom::CommandSet::CMoveRSP duplicateMoveWarning(
			87,
			classUID,
			dicom::Status::PENDING,
			dicom::DataSetStatus::NO_DATA_SET);
		addRetrieveCounters(duplicateMoveWarning, true, true, true, true);
		duplicateMoveWarning.Put<dicom::VR_US>(dicom::TAG_NUM_WARN_SUBOP, UINT16(0));
		assertCMoveResponseRejected(duplicateMoveWarning, 87);

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CGetRSP successWithRemaining(
				89,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			successWithRemaining.setRemaining(0);
			successWithRemaining.setCompleted(3);
			successWithRemaining.setFailed(0);
			successWithRemaining.setWarning(0);
			scpSide.WriteCommand(successWithRemaining, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(89);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 3);
			assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CGetRSP warningWithRemaining(
				91,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			warningWithRemaining.setRemaining(0);
			warningWithRemaining.setCompleted(2);
			warningWithRemaining.setFailed(0);
			warningWithRemaining.setWarning(1);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.20"));
			scpSide.WriteCommand(warningWithRemaining, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(91);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::WARNING);
			assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 2);
			assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 1);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CGetRSP failureWithRemaining(
				93,
				classUID,
				UINT16(0xa702),
				dicom::DataSetStatus::YES_DATA_SET);
			failureWithRemaining.setRemaining(0);
			failureWithRemaining.setCompleted(1);
			failureWithRemaining.setFailed(2);
			failureWithRemaining.setWarning(0);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.21"));
			scpSide.WriteCommand(failureWithRemaining, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(93);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == UINT16(0xa702));
			assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 1);
			assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 2);
			assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CMoveRSP successWithRemaining(
				95,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::NO_DATA_SET);
			successWithRemaining.setRemaining(0);
			successWithRemaining.setCompleted(3);
			successWithRemaining.setFailed(0);
			successWithRemaining.setWarning(0);
			scpSide.WriteCommand(successWithRemaining, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(95);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::SUCCESS);
			assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 3);
			assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CMoveRSP warningWithRemaining(
				97,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			warningWithRemaining.setRemaining(0);
			warningWithRemaining.setCompleted(2);
			warningWithRemaining.setFailed(0);
			warningWithRemaining.setWarning(1);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.22"));
			scpSide.WriteCommand(warningWithRemaining, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(97);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::WARNING);
			assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 2);
			assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 1);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);
			dicom::CommandSet::CMoveRSP failureWithRemaining(
				99,
				classUID,
				UINT16(0xa702),
				dicom::DataSetStatus::YES_DATA_SET);
			failureWithRemaining.setRemaining(0);
			failureWithRemaining.setCompleted(1);
			failureWithRemaining.setFailed(2);
			failureWithRemaining.setWarning(0);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.23"));
			scpSide.WriteCommand(failureWithRemaining, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(99);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == UINT16(0xa702));
			assert(get<UINT16>(response, dicom::TAG_NUM_REMAIN_SUBOP) == 0);
			assert(get<UINT16>(response, dicom::TAG_NUM_COMPL_SUBOP) == 1);
			assert(get<UINT16>(response, dicom::TAG_NUM_FAIL_SUBOP) == 2);
			assert(get<UINT16>(response, dicom::TAG_NUM_WARN_SUBOP) == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CGetRSP warningWithIdentifier(
				101,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NO_FAILED_LIST"));
			scpSide.WriteCommand(warningWithIdentifier, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(101);

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

			dicom::CommandSet::CGetRSP cancelWithSpecificCharacterSet(
				123,
				classUID,
				dicom::Status::CANCEL,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.8"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(cancelWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(123);

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

			dicom::CommandSet::CGetRSP failureWithSpecificCharacterSet(
				119,
				classUID,
				UINT16(0xa702),
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.6"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(failureWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(119);

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

			dicom::CommandSet::CMoveRSP cancelWithSpecificCharacterSet(
				125,
				classUID,
				dicom::Status::CANCEL,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.9"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(cancelWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(125);

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

			dicom::CommandSet::CGetRSP warningWithSpecificCharacterSet(
				117,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.5"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(warningWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(117);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			bool rejected = false;
			try
			{
				scu.readRSP(
					status,
					response,
					data,
					[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
					{
					});
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

			dicom::CommandSet::CMoveRSP failureWithSpecificCharacterSet(
				121,
				classUID,
				UINT16(0xa801),
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.7"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(failureWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(121);

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

			dicom::CommandSet::CGetRSP warningWithIdentifier(
				103,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.1"));
			scpSide.WriteCommand(warningWithIdentifier, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(103);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::WARNING);
			assert(data.exists(dicom::TAG_FAILED_SOPINSTUID_LIST));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CGetRSP warningWithSpecificCharacterSet(
				113,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.3"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(warningWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(113);

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

			dicom::CommandSet::CGetRSP warningWithEmptyFailedUID(
				109,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID(""));
			scpSide.WriteCommand(warningWithEmptyFailedUID, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(109);

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

			dicom::CommandSet::CMoveRSP warningWithIdentifier(
				105,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NO_FAILED_LIST"));
			scpSide.WriteCommand(warningWithIdentifier, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(105);

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

			dicom::CommandSet::CMoveRSP warningWithIdentifier(
				107,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.2"));
			scpSide.WriteCommand(warningWithIdentifier, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(107);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status, response, data);
			assert(status == dicom::Status::WARNING);
			assert(data.exists(dicom::TAG_FAILED_SOPINSTUID_LIST));
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CMoveRSP warningWithSpecificCharacterSet(
				115,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID("1.2.826.0.1.3680043.10.1553.14.4"));
			identifier.Put<dicom::VR_CS>(dicom::TAG_CHAR_SET, std::string("ISO_IR 100"));
			scpSide.WriteCommand(warningWithSpecificCharacterSet, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(115);

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

			dicom::CommandSet::CMoveRSP warningWithEmptyFailedUID(
				111,
				classUID,
				dicom::Status::WARNING,
				dicom::DataSetStatus::YES_DATA_SET);
			dicom::DataSet identifier;
			identifier.Put<dicom::VR_UI>(
				dicom::TAG_FAILED_SOPINSTUID_LIST,
				dicom::UID(""));
			scpSide.WriteCommand(warningWithEmptyFailedUID, classUID);
			scpSide.WriteDataSet(identifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(111);

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

			dicom::CommandSet::CGetRSP pendingWithoutCounters(
				43,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(pendingWithoutCounters, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(43);

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

			dicom::CommandSet::CGetRSP pendingWithoutCounters(
				45,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(pendingWithoutCounters, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(45);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			bool rejected = false;
			try
			{
				scu.readRSP(
					status,
					response,
					data,
					[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
					{
					});
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

			dicom::CommandSet::CMoveRSP pendingWithoutCounters(
				47,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(pendingWithoutCounters, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(47);

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

			dicom::CommandSet::CGetRSP successWithIdentifier(
				39,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::YES_DATA_SET);
			scpSide.WriteCommand(successWithIdentifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(39);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			bool rejected = false;
			try
			{
				scu.readRSP(
					status,
					response,
					data,
					[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
					{
					});
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

			dicom::CommandSet::CGetRSP successWithIdentifier(
				39,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::YES_DATA_SET);
			scpSide.WriteCommand(successWithIdentifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(39);

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

			dicom::CommandSet::CMoveRSP pendingWithIdentifier(
				41,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::YES_DATA_SET);
			scpSide.WriteCommand(pendingWithIdentifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(41);

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

		const auto assertCEchoResponseRejected =
			[](const std::function<void(dicom::DataSet&, UINT16)>& mutateResponse)
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
				dicom::CommandSet::CEchoRSP responseCommand(
					messageID,
					dicom::VERIFICATION_SOP_CLASS);
				mutateResponse(responseCommand, messageID);
				scpSide.WriteCommand(responseCommand, dicom::VERIFICATION_SOP_CLASS);

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
			};

		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_ECHO_RSP);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16 messageID)
			{
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, messageID);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::VERIFICATION_SOP_CLASS);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(
					dicom::TAG_DATA_SET_TYPE,
					dicom::DataSetStatus::NO_DATA_SET);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::SUCCESS);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_LO>(
					dicom::TAG_ERR_COMMENT,
					std::string("first"));
				response.Put<dicom::VR_LO>(
					dicom::TAG_ERR_COMMENT,
					std::string("second"));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(1));
				response.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(2));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(dicom::TAG_NUM_REMAIN_SUBOP, UINT16(0));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_STATUS);
				response.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::WARNING);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_CMD_FIELD);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_MSG_ID_RSP);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_STATUS);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_DATA_SET_TYPE);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_STORE_RSP);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16 messageID)
			{
				response.erase(dicom::TAG_MSG_ID_RSP);
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(messageID + 2));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_AFF_SOP_CLASS_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.erase(dicom::TAG_AFF_SOP_CLASS_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::UID(""));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_INST_UID,
					dicom::UID("1.2.826.0.1.3680043.10.1553.12.31"));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_UI>(
					dicom::TAG_REQ_SOP_CLASS_UID,
					dicom::VERIFICATION_SOP_CLASS);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_UI>(
					dicom::TAG_REQ_SOP_INST_UID,
					dicom::UID("1.2.826.0.1.3680043.10.1553.12.32"));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
			});
		assertCEchoResponseRejected(
			[](dicom::DataSet& response, UINT16)
			{
				response.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(33));
			});
		const auto assertCEchoResponseAcceptedStatus =
			[](UINT16 expectedStatus)
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
				dicom::CommandSet::CEchoRSP responseCommand(
					messageID,
					dicom::VERIFICATION_SOP_CLASS);
				responseCommand.erase(dicom::TAG_STATUS);
				responseCommand.Put<dicom::VR_US>(dicom::TAG_STATUS, expectedStatus);
				scpSide.WriteCommand(responseCommand, dicom::VERIFICATION_SOP_CLASS);

				UINT16 status = 0;
				dicom::DataSet response;
				scu.readRSP(status, response);
				assert(status == expectedStatus);
			};

		assertCEchoResponseAcceptedStatus(0x0122);
		assertCEchoResponseAcceptedStatus(0x0210);
		assertCEchoResponseAcceptedStatus(0x0211);
		assertCEchoResponseAcceptedStatus(0x0212);

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
			dicom::CommandSet::CEchoRSP responseCommand(
				messageID,
				dicom::VERIFICATION_SOP_CLASS);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpSide.WriteCommand(responseCommand, dicom::VERIFICATION_SOP_CLASS);

			UINT16 status = 0;
			dicom::DataSet response;
			scu.readRSP(status, response);
			assert(status == dicom::Status::SUCCESS);
		}

		const auto assertCStoreResponseRejected =
			[](const std::function<void(dicom::DataSet&, UINT16, const dicom::UID&)>& mutateResponse)
			{
				const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.12.30");
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
				dicom::CommandSet::CStoreRSP responseCommand(
					messageID,
					dicom::CT_IMAGE_STORAGE_SOP_CLASS,
					instanceUID,
					dicom::Status::SUCCESS);
				mutateResponse(responseCommand, messageID, instanceUID);
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
			};

		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_STORE_RSP);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16 messageID, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, messageID);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(
					dicom::TAG_DATA_SET_TYPE,
					dicom::DataSetStatus::NO_DATA_SET);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::SUCCESS);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_LO>(
					dicom::TAG_ERR_COMMENT,
					std::string("first"));
				response.Put<dicom::VR_LO>(
					dicom::TAG_ERR_COMMENT,
					std::string("second"));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(1));
				response.Put<dicom::VR_US>(dicom::TAG_ERR_ID, UINT16(2));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_NUM_REMAIN_SUBOP, UINT16(0));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_STATUS);
				response.Put<dicom::VR_US>(dicom::TAG_STATUS, dicom::Status::CANCEL);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_CMD_FIELD);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_MSG_ID_RSP);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_STATUS);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_DATA_SET_TYPE);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_CMD_FIELD);
				response.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_ECHO_RSP);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16 messageID, const dicom::UID&)
			{
				response.erase(dicom::TAG_MSG_ID_RSP);
				response.Put<dicom::VR_US>(dicom::TAG_MSG_ID_RSP, UINT16(messageID + 2));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_AFF_SOP_CLASS_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::VERIFICATION_SOP_CLASS);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_AFF_SOP_CLASS_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_CLASS_UID,
					dicom::UID(""));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.erase(dicom::TAG_AFF_SOP_INST_UID);
				response.Put<dicom::VR_UI>(
					dicom::TAG_AFF_SOP_INST_UID,
					dicom::UID(""));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_UI>(
					dicom::TAG_REQ_SOP_CLASS_UID,
					dicom::CT_IMAGE_STORAGE_SOP_CLASS);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID& instanceUID)
			{
				response.Put<dicom::VR_UI>(dicom::TAG_REQ_SOP_INST_UID, instanceUID);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
			});
		assertCStoreResponseRejected(
			[](dicom::DataSet& response, UINT16, const dicom::UID&)
			{
				response.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(31));
			});
		const auto assertCStoreResponseAcceptedStatus =
			[](UINT16 expectedStatus)
			{
				const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.12.40");
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
				dicom::CommandSet::CStoreRSP responseCommand(
					messageID,
					dicom::CT_IMAGE_STORAGE_SOP_CLASS,
					instanceUID,
					expectedStatus);
				scpSide.WriteCommand(responseCommand, dicom::CT_IMAGE_STORAGE_SOP_CLASS);

				UINT16 status = 0;
				dicom::DataSet response;
				scu.readRSP(status, response);
				assert(status == expectedStatus);
			};

		assertCStoreResponseAcceptedStatus(0xb000);
		assertCStoreResponseAcceptedStatus(0xa700);
		assertCStoreResponseAcceptedStatus(0xc123);
		assertCStoreResponseAcceptedStatus(0x0117);
		assertCStoreResponseAcceptedStatus(0x0122);
		assertCStoreResponseAcceptedStatus(0x0124);
		assertCStoreResponseAcceptedStatus(0x0210);
		assertCStoreResponseAcceptedStatus(0x0211);
		assertCStoreResponseAcceptedStatus(0x0212);

		{
			const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.12.41");
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
			dicom::CommandSet::CStoreRSP responseCommand(
				messageID,
				dicom::CT_IMAGE_STORAGE_SOP_CLASS,
				instanceUID,
				dicom::Status::SUCCESS);
			responseCommand.erase(dicom::TAG_AFF_SOP_CLASS_UID);
			scpSide.WriteCommand(responseCommand, dicom::CT_IMAGE_STORAGE_SOP_CLASS);

			UINT16 status = 0;
			dicom::DataSet response;
			scu.readRSP(status, response);
			assert(status == dicom::Status::SUCCESS);
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
			const dicom::UID instanceUID("1.2.826.0.1.3680043.10.1553.12.7");
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
			dicom::CommandSet::CStoreRSP duplicateInstance(
				messageID,
				dicom::CT_IMAGE_STORAGE_SOP_CLASS,
				instanceUID,
				dicom::Status::SUCCESS);
			duplicateInstance.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_INST_UID, instanceUID);
			scpSide.WriteCommand(duplicateInstance, dicom::CT_IMAGE_STORAGE_SOP_CLASS);

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

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CFindRSP pendingNoIdentifier(
				31,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::NO_DATA_SET);
			scpSide.WriteCommand(pendingNoIdentifier, classUID);

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(31);

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

			dicom::CommandSet::CFindRSP finalWithIdentifier(
				33,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::YES_DATA_SET);
			scpSide.WriteCommand(finalWithIdentifier, classUID);

			TestCFindSCU scu(scuSide, classUID);
			scu.setLastMessageID(33);

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

			dicom::CommandSet::CGetRSP pendingWithIdentifier(
				35,
				classUID,
				dicom::Status::PENDING,
				dicom::DataSetStatus::YES_DATA_SET);
			scpSide.WriteCommand(pendingWithIdentifier, classUID);

			TestCGetSCU scu(scuSide, classUID);
			scu.setLastMessageID(35);

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

			dicom::CommandSet::CMoveRSP successWithIdentifier(
				37,
				classUID,
				dicom::Status::SUCCESS,
				dicom::DataSetStatus::YES_DATA_SET);
			scpSide.WriteCommand(successWithIdentifier, classUID);

			TestCMoveSCU scu(scuSide, classUID);
			scu.setLastMessageID(37);

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

	void checkRetrieveFailedSOPInstanceUIDListEmission()
	{
		const dicom::UID getClassUID("1.2.840.10008.5.1.4.1.2.2.3");
		const dicom::UID moveClassUID("1.2.840.10008.5.1.4.1.2.2.2");
		const dicom::UID storeClassUID = dicom::CT_IMAGE_STORAGE_SOP_CLASS;
		const dicom::UID studyUID("1.2.826.0.1.3680043.10.1553.15");
		const dicom::UID failedUID("1.2.826.0.1.3680043.10.1553.15.1");
		const dicom::UID successUID("1.2.826.0.1.3680043.10.1553.15.2");

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService getSCUSide(sockets[0], getClassUID);
			PairedService getSCPSide(sockets[1], getClassUID);

			dicom::CGetSCU getSCU(getSCUSide, getClassUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			getSCU.writeRQ(query);

			dicom::DataSet request;
			requireRead(getSCPSide, request);

			dicom::CSubOperationResult result(dicom::Status::WARNING,0,1,1,0);
			result.failedSOPInstanceUIDs.push_back(failedUID);
			dicom::HandleCGet(
				dicom::CGetStatusFunction(
					[&](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet& identifier)
					{
						assert(get<std::string>(identifier,dicom::TAG_QR_LEVEL) == "STUDY");
						return result;
					}),
				getSCPSide,
				request,
				getClassUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			getSCU.readRSP(status,response,data);
			assert(status == dicom::Status::WARNING);
			assert(get<UINT16>(response,dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
			assert(get<UINT16>(response,dicom::TAG_NUM_COMPL_SUBOP) == 1);
			assert(get<UINT16>(response,dicom::TAG_NUM_FAIL_SUBOP) == 1);
			assert(get<dicom::UID>(data,dicom::TAG_FAILED_SOPINSTUID_LIST) == failedUID);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService moveSCUSide(sockets[0], moveClassUID);
			PairedService moveSCPSide(sockets[1], moveClassUID);

			dicom::CMoveSCU moveSCU(moveSCUSide, moveClassUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			moveSCU.writeRQ("DEST_AE", query);

			dicom::DataSet request;
			requireRead(moveSCPSide, request);

			dicom::CSubOperationResult result(dicom::Status::WARNING,0,1,1,0);
			result.failedSOPInstanceUIDs.push_back(failedUID);
			dicom::HandleCMove(
				dicom::CMoveStatusFunction(
					[&](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet& identifier)
					{
						assert(get<std::string>(identifier,dicom::TAG_QR_LEVEL) == "STUDY");
						return result;
					}),
				moveSCPSide,
				request,
				moveClassUID);

			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			moveSCU.readRSP(status,response,data);
			assert(status == dicom::Status::WARNING);
			assert(get<UINT16>(response,dicom::TAG_DATA_SET_TYPE) == dicom::DataSetStatus::YES_DATA_SET);
			assert(get<UINT16>(response,dicom::TAG_NUM_COMPL_SUBOP) == 1);
			assert(get<UINT16>(response,dicom::TAG_NUM_FAIL_SUBOP) == 1);
			assert(get<dicom::UID>(data,dicom::TAG_FAILED_SOPINSTUID_LIST) == failedUID);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService getSCUSide(sockets[0], getClassUID);
			PairedService getSCPSide(sockets[1], getClassUID);

			dicom::CGetSCU getSCU(getSCUSide, getClassUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			getSCU.writeRQ(query);

			dicom::DataSet request;
			requireRead(getSCPSide, request);

			dicom::CSubOperationResult result(dicom::Status::SUCCESS,0,1,0,0);
			result.failedSOPInstanceUIDs.push_back(failedUID);
			bool rejected = false;
			try
			{
				dicom::HandleCGet(
					dicom::CGetStatusFunction(
						[&](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
							return result;
						}),
					getSCPSide,
					request,
					getClassUID);
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
			PairedService getSCUSide(sockets[0], getClassUID);
			PairedService getSCPSide(sockets[1], getClassUID);

			dicom::CGetSCU getSCU(getSCUSide, getClassUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			getSCU.writeRQ(query);

			dicom::DataSet request;
			requireRead(getSCPSide, request);

			dicom::CSubOperationResult result(dicom::Status::WARNING,0,0,1,0);
			result.failedSOPInstanceUIDs.push_back(dicom::UID(""));
			bool rejected = false;
			try
			{
				dicom::HandleCGet(
					dicom::CGetStatusFunction(
						[&](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
							return result;
						}),
					getSCPSide,
					request,
					getClassUID);
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
			PairedService moveSCUSide(sockets[0], moveClassUID);
			PairedService moveSCPSide(sockets[1], moveClassUID);

			dicom::CMoveSCU moveSCU(moveSCUSide, moveClassUID);
			dicom::DataSet query;
			query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			moveSCU.writeRQ("DEST_AE", query);

			dicom::DataSet request;
			requireRead(moveSCPSide, request);

			dicom::CSubOperationResult result(dicom::Status::SUCCESS,0,1,0,0);
			result.failedSOPInstanceUIDs.push_back(failedUID);
			bool rejected = false;
			try
			{
				dicom::HandleCMove(
					dicom::CMoveStatusFunction(
						[&](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
							return result;
						}),
					moveSCPSide,
					request,
					moveClassUID);
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
			PairedService scuSide(sockets[0], storeClassUID);
			PairedService scpSide(sockets[1], storeClassUID);

			dicom::DataSet failed;
			failed.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			failed.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, failedUID);
			failed.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			dicom::DataSet success;
			success.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			success.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			success.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			dicom::Sequence instances;
			instances.push_back(failed);
			instances.push_back(success);

			std::thread responder(
				[&]()
				{
					for(int index=0; index<2; ++index)
					{
						dicom::DataSet command;
						dicom::DataSet stored;
						requireRead(scpSide, command);
						requireRead(scpSide, stored);
						const UINT16 messageID = get<UINT16>(command,dicom::TAG_MSG_ID);
						const dicom::UID instanceUID = get<dicom::UID>(command,dicom::TAG_AFF_SOP_INST_UID);
						const UINT16 status = index == 0 ? UINT16(0xa700) : dicom::Status::SUCCESS;
						dicom::CommandSet::CStoreRSP response(
							messageID,
							storeClassUID,
							instanceUID,
							status);
						scpSide.WriteCommand(response,storeClassUID);
					}
				});

			const dicom::CSubOperationResult result =
				dicom::SendCGetStoreSubOperations(scuSide,instances);
			responder.join();
			assert(result.status == dicom::Status::WARNING);
			assert(result.completed == 1);
			assert(result.failed == 1);
			assert(result.failedSOPInstanceUIDs.size() == 1);
			assert(result.failedSOPInstanceUIDs.at(0) == failedUID);
		}

		{
			NullService service;
			dicom::DataSet first;
			first.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			first.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, failedUID);
			dicom::DataSet second;
			second.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			second.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			dicom::Sequence instances;
			instances.push_back(first);
			instances.push_back(second);

			service.RequestCancel(37);
			const dicom::CSubOperationResult result =
				dicom::SendCGetStoreSubOperations(service,instances,37);
			assert(result.status == dicom::Status::CANCEL);
			assert(result.remaining == 2);
			assert(result.completed == 0);
			assert(result.failed == 0);
			assert(result.warning == 0);
			assert(result.failedSOPInstanceUIDs.empty());
		}

		{
			NullService service;
			dicom::DataSet first;
			first.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			first.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, failedUID);
			dicom::DataSet second;
			second.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			second.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			dicom::Sequence instances;
			instances.push_back(first);
			instances.push_back(second);

			service.RequestCancel(41);
			const dicom::CSubOperationResult result =
				dicom::SendCGetStoreSubOperations(service,instances);
			assert(result.status == dicom::Status::CANCEL);
			assert(result.remaining == 2);
			assert(result.completed == 0);
			assert(result.failed == 0);
			assert(result.warning == 0);
			assert(result.failedSOPInstanceUIDs.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], storeClassUID);
			PairedService scpSide(sockets[1], storeClassUID);
			dicom::DataSet storedInstance;
			storedInstance.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			storedInstance.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			storedInstance.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			dicom::Sequence instances;
			instances.push_back(storedInstance);

			scuSide.RequestCancel(99);
			std::thread responder(
				[&]()
				{
					dicom::DataSet command;
					dicom::DataSet stored;
					requireRead(scpSide, command);
					requireRead(scpSide, stored);
					const UINT16 messageID = get<UINT16>(command,dicom::TAG_MSG_ID);
					const dicom::UID instanceUID = get<dicom::UID>(command,dicom::TAG_AFF_SOP_INST_UID);
					dicom::CommandSet::CStoreRSP response(
						messageID,
						storeClassUID,
						instanceUID,
						dicom::Status::SUCCESS);
					scpSide.WriteCommand(response,storeClassUID);
				});

			const dicom::CSubOperationResult result =
				dicom::SendCGetStoreSubOperations(scuSide,instances,37);
			responder.join();
			assert(result.status == dicom::Status::SUCCESS);
			assert(result.remaining == 0);
			assert(result.completed == 1);
			assert(result.failed == 0);
			assert(result.warning == 0);
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService scuSide(sockets[0], storeClassUID);
			PairedService scpSide(sockets[1], storeClassUID);

			dicom::DataSet failed;
			failed.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			failed.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, failedUID);
			failed.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			dicom::DataSet success;
			success.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			success.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			success.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			dicom::Sequence instances;
			instances.push_back(failed);
			instances.push_back(success);

			std::thread responder(
				[&]()
				{
					for(int index=0; index<2; ++index)
					{
						dicom::DataSet command;
						dicom::DataSet stored;
						requireRead(scpSide, command);
						requireRead(scpSide, stored);
						assert(get<std::string>(command,dicom::TAG_MOVE_ORIG_AET) == "MOVE_AE");
						assert(get<UINT16>(command,dicom::TAG_MOVE_ORIG_MSG_ID) == 19);
						const UINT16 messageID = get<UINT16>(command,dicom::TAG_MSG_ID);
						const dicom::UID instanceUID = get<dicom::UID>(command,dicom::TAG_AFF_SOP_INST_UID);
						const UINT16 status = index == 0 ? UINT16(0xa700) : dicom::Status::SUCCESS;
						dicom::CommandSet::CStoreRSP response(
							messageID,
							storeClassUID,
							instanceUID,
							status);
						scpSide.WriteCommand(response,storeClassUID);
					}
				});

			const dicom::CSubOperationResult result =
				dicom::SendCMoveStoreSubOperations(scuSide,instances,"MOVE_AE",19);
			responder.join();
			assert(result.status == dicom::Status::WARNING);
			assert(result.completed == 1);
			assert(result.failed == 1);
			assert(result.failedSOPInstanceUIDs.size() == 1);
			assert(result.failedSOPInstanceUIDs.at(0) == failedUID);
		}

		{
			NullService destination;
			NullService cancelService;
			dicom::DataSet first;
			first.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			first.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, failedUID);
			dicom::DataSet second;
			second.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			second.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			dicom::Sequence instances;
			instances.push_back(first);
			instances.push_back(second);

			cancelService.RequestCancel(39);
			const dicom::CSubOperationResult result =
				dicom::SendCMoveStoreSubOperations(
					destination,
					instances,
					"MOVE_AE",
					19,
					cancelService,
					39);
			assert(result.status == dicom::Status::CANCEL);
			assert(result.remaining == 2);
			assert(result.completed == 0);
			assert(result.failed == 0);
			assert(result.warning == 0);
			assert(result.failedSOPInstanceUIDs.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService destination(sockets[0], storeClassUID);
			PairedService storageSCP(sockets[1], storeClassUID);
			NullService cancelService;
			dicom::DataSet storedInstance;
			storedInstance.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, storeClassUID);
			storedInstance.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, successUID);
			storedInstance.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID, studyUID);
			dicom::Sequence instances;
			instances.push_back(storedInstance);

			cancelService.RequestCancel(99);
			std::thread responder(
				[&]()
				{
					dicom::DataSet command;
					dicom::DataSet stored;
					requireRead(storageSCP, command);
					requireRead(storageSCP, stored);
					assert(get<std::string>(command,dicom::TAG_MOVE_ORIG_AET) == "MOVE_AE");
					assert(get<UINT16>(command,dicom::TAG_MOVE_ORIG_MSG_ID) == 19);
					const UINT16 messageID = get<UINT16>(command,dicom::TAG_MSG_ID);
					const dicom::UID instanceUID = get<dicom::UID>(command,dicom::TAG_AFF_SOP_INST_UID);
					dicom::CommandSet::CStoreRSP response(
						messageID,
						storeClassUID,
						instanceUID,
						dicom::Status::SUCCESS);
					storageSCP.WriteCommand(response,storeClassUID);
				});

			const dicom::CSubOperationResult result =
				dicom::SendCMoveStoreSubOperations(
					destination,
					instances,
					"MOVE_AE",
					19,
					cancelService,
					39);
			responder.join();
			assert(result.status == dicom::Status::SUCCESS);
			assert(result.remaining == 0);
			assert(result.completed == 1);
			assert(result.failed == 0);
			assert(result.warning == 0);
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

		const auto assertEchoRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCEcho(service,request,dicom::VERIFICATION_SOP_CLASS);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};

		dicom::CommandSet::CEchoRQ duplicateEchoCommandField(74,dicom::VERIFICATION_SOP_CLASS);
		duplicateEchoCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_ECHO_RQ);
		assertEchoRequestRejected(duplicateEchoCommandField);

		dicom::CommandSet::CEchoRQ duplicateEchoSOPClassUID(76,dicom::VERIFICATION_SOP_CLASS);
		duplicateEchoSOPClassUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_CLASS_UID,
			dicom::VERIFICATION_SOP_CLASS);
		assertEchoRequestRejected(duplicateEchoSOPClassUID);

		dicom::CommandSet::CEchoRQ duplicateEchoMessageID(78,dicom::VERIFICATION_SOP_CLASS);
		duplicateEchoMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(78));
		assertEchoRequestRejected(duplicateEchoMessageID);

		dicom::CommandSet::CEchoRQ duplicateEchoDataSetType(80,dicom::VERIFICATION_SOP_CLASS);
		duplicateEchoDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::NO_DATA_SET);
		assertEchoRequestRejected(duplicateEchoDataSetType);

		dicom::CommandSet::CEchoRQ missingEchoCommandField(106,dicom::VERIFICATION_SOP_CLASS);
		missingEchoCommandField.erase(dicom::TAG_CMD_FIELD);
		assertEchoRequestRejected(missingEchoCommandField);

		dicom::CommandSet::CEchoRQ missingEchoSOPClassUID(108,dicom::VERIFICATION_SOP_CLASS);
		missingEchoSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertEchoRequestRejected(missingEchoSOPClassUID);

		dicom::CommandSet::CEchoRQ missingEchoMessageID(110,dicom::VERIFICATION_SOP_CLASS);
		missingEchoMessageID.erase(dicom::TAG_MSG_ID);
		assertEchoRequestRejected(missingEchoMessageID);

		dicom::CommandSet::CEchoRQ missingEchoDataSetType(112,dicom::VERIFICATION_SOP_CLASS);
		missingEchoDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertEchoRequestRejected(missingEchoDataSetType);

		dicom::CommandSet::CEchoRQ wrongEchoCommandField(146,dicom::VERIFICATION_SOP_CLASS);
		wrongEchoCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongEchoCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_STORE_RQ);
		assertEchoRequestRejected(wrongEchoCommandField);

		dicom::CommandSet::CEchoRQ emptyEchoSOPClassUID(218,dicom::VERIFICATION_SOP_CLASS);
		emptyEchoSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		emptyEchoSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, dicom::UID(""));
		assertEchoRequestRejected(emptyEchoSOPClassUID);

		dicom::CommandSet::CEchoRQ echoRequestedClassUID(164,dicom::VERIFICATION_SOP_CLASS);
		echoRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			dicom::VERIFICATION_SOP_CLASS);
		assertEchoRequestRejected(echoRequestedClassUID);

		dicom::CommandSet::CEchoRQ echoRequestedInstanceUID(166,dicom::VERIFICATION_SOP_CLASS);
		echoRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.166"));
		assertEchoRequestRejected(echoRequestedInstanceUID);

		dicom::CommandSet::CEchoRQ echoAffectedInstanceUID(184,dicom::VERIFICATION_SOP_CLASS);
		echoAffectedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.184"));
		assertEchoRequestRejected(echoAffectedInstanceUID);

		dicom::CommandSet::CEchoRQ echoPriority(192,dicom::VERIFICATION_SOP_CLASS);
		echoPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		assertEchoRequestRejected(echoPriority);

		dicom::CommandSet::CEchoRQ echoMoveDestination(194,dicom::VERIFICATION_SOP_CLASS);
		echoMoveDestination.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		assertEchoRequestRejected(echoMoveDestination);

		dicom::CommandSet::CEchoRQ echoMoveOriginatorAET(196,dicom::VERIFICATION_SOP_CLASS);
		echoMoveOriginatorAET.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
		assertEchoRequestRejected(echoMoveOriginatorAET);

		dicom::CommandSet::CEchoRQ echoMoveOriginatorMessageID(198,dicom::VERIFICATION_SOP_CLASS);
		echoMoveOriginatorMessageID.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(198));
		assertEchoRequestRejected(echoMoveOriginatorMessageID);

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

		const auto assertStoreRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCStore(
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
						},
						service,
						request,
						dicom::CT_IMAGE_STORAGE_SOP_CLASS);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertFindRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCFind(
						dicom::CFindStatusFunction(
							[](dicom::ServiceBase&, dicom::DataSet&, dicom::Sequence&)
							{
								return dicom::Status::SUCCESS;
							}),
						service,
						request,
						classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertGetRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCGet(
						dicom::CGetStatusFunction(
							[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
							{
								return dicom::CSubOperationResult(dicom::Status::SUCCESS,0,0,0,0);
							}),
						service,
						request,
						classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertLegacyGetRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCGet(
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
						},
						service,
						request,
						classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertMoveRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCMove(
						dicom::CMoveStatusFunction(
							[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
							{
								return dicom::CSubOperationResult(dicom::Status::SUCCESS,0,0,0,0);
							}),
						service,
						request,
						classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};
		const auto assertLegacyMoveRequestRejected =
			[&](const dicom::DataSet& request)
			{
				bool rejected = false;
				try
				{
					dicom::HandleCMove(
						[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
						{
						},
						service,
						request,
						classUID);
				}
				catch(const std::exception&)
				{
					rejected = true;
				}
				assert(rejected);
			};

		dicom::CommandSet::CStoreRQ duplicateStoreCommandField(
			82,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.82"));
		duplicateStoreCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_STORE_RQ);
		assertStoreRequestRejected(duplicateStoreCommandField);

		dicom::CommandSet::CStoreRQ duplicateStoreSOPClassUID(
			84,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.84"));
		duplicateStoreSOPClassUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_CLASS_UID,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		assertStoreRequestRejected(duplicateStoreSOPClassUID);

		dicom::CommandSet::CStoreRQ duplicateStoreMessageID(
			86,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.86"));
		duplicateStoreMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(86));
		assertStoreRequestRejected(duplicateStoreMessageID);

		dicom::CommandSet::CStoreRQ duplicateStoreDataSetType(
			88,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.88"));
		duplicateStoreDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::YES_DATA_SET);
		assertStoreRequestRejected(duplicateStoreDataSetType);

		dicom::CommandSet::CStoreRQ missingStoreCommandField(
			114,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.114"));
		missingStoreCommandField.erase(dicom::TAG_CMD_FIELD);
		assertStoreRequestRejected(missingStoreCommandField);

		dicom::CommandSet::CStoreRQ missingStoreSOPClassUID(
			116,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.116"));
		missingStoreSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertStoreRequestRejected(missingStoreSOPClassUID);

		dicom::CommandSet::CStoreRQ missingStoreMessageID(
			118,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.118"));
		missingStoreMessageID.erase(dicom::TAG_MSG_ID);
		assertStoreRequestRejected(missingStoreMessageID);

		dicom::CommandSet::CStoreRQ missingStoreDataSetType(
			120,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.120"));
		missingStoreDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertStoreRequestRejected(missingStoreDataSetType);

		dicom::CommandSet::CStoreRQ wrongStoreCommandField(
			148,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.148"));
		wrongStoreCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongStoreCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_ECHO_RQ);
		assertStoreRequestRejected(wrongStoreCommandField);

		dicom::CommandSet::CStoreRQ wrongStoreSOPClassUID(
			150,
			wrongClassUID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.150"));
		assertStoreRequestRejected(wrongStoreSOPClassUID);

		dicom::CommandSet::CStoreRQ emptyStoreSOPClassUID(
			220,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.220"));
		emptyStoreSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		emptyStoreSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, dicom::UID(""));
		assertStoreRequestRejected(emptyStoreSOPClassUID);

		dicom::CommandSet::CStoreRQ storeRequestedClassUID(
			168,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.168"));
		storeRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		assertStoreRequestRejected(storeRequestedClassUID);

		dicom::CommandSet::CStoreRQ storeRequestedInstanceUID(
			170,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.170"));
		storeRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.171"));
		assertStoreRequestRejected(storeRequestedInstanceUID);

		dicom::CommandSet::CStoreRQ storeMoveDestination(
			200,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.200"));
		storeMoveDestination.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		assertStoreRequestRejected(storeMoveDestination);

		dicom::CommandSet::CGetRQ duplicateGetCommandField(90,classUID);
		duplicateGetCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_GET_RQ);
		assertGetRequestRejected(duplicateGetCommandField);
		assertLegacyGetRequestRejected(duplicateGetCommandField);

		dicom::CommandSet::CGetRQ duplicateGetSOPClassUID(92,classUID);
		duplicateGetSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
		assertGetRequestRejected(duplicateGetSOPClassUID);
		assertLegacyGetRequestRejected(duplicateGetSOPClassUID);

		dicom::CommandSet::CGetRQ duplicateGetMessageID(94,classUID);
		duplicateGetMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(94));
		assertGetRequestRejected(duplicateGetMessageID);
		assertLegacyGetRequestRejected(duplicateGetMessageID);

		dicom::CommandSet::CGetRQ duplicateGetDataSetType(96,classUID);
		duplicateGetDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::YES_DATA_SET);
		assertGetRequestRejected(duplicateGetDataSetType);
		assertLegacyGetRequestRejected(duplicateGetDataSetType);

		dicom::CommandSet::CGetRQ missingGetCommandField(122,classUID);
		missingGetCommandField.erase(dicom::TAG_CMD_FIELD);
		assertGetRequestRejected(missingGetCommandField);
		assertLegacyGetRequestRejected(missingGetCommandField);

		dicom::CommandSet::CGetRQ missingGetSOPClassUID(124,classUID);
		missingGetSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertGetRequestRejected(missingGetSOPClassUID);
		assertLegacyGetRequestRejected(missingGetSOPClassUID);

		dicom::CommandSet::CGetRQ missingGetMessageID(126,classUID);
		missingGetMessageID.erase(dicom::TAG_MSG_ID);
		assertGetRequestRejected(missingGetMessageID);
		assertLegacyGetRequestRejected(missingGetMessageID);

		dicom::CommandSet::CGetRQ missingGetDataSetType(128,classUID);
		missingGetDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertGetRequestRejected(missingGetDataSetType);
		assertLegacyGetRequestRejected(missingGetDataSetType);

		dicom::CommandSet::CGetRQ wrongGetCommandField(152,classUID);
		wrongGetCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongGetCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_FIND_RQ);
		assertGetRequestRejected(wrongGetCommandField);
		assertLegacyGetRequestRejected(wrongGetCommandField);

		dicom::CommandSet::CGetRQ wrongGetSOPClassUID(154,wrongClassUID);
		assertGetRequestRejected(wrongGetSOPClassUID);
		assertLegacyGetRequestRejected(wrongGetSOPClassUID);

		dicom::CommandSet::CGetRQ getRequestedClassUID(172,classUID);
		getRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			classUID);
		assertGetRequestRejected(getRequestedClassUID);
		assertLegacyGetRequestRejected(getRequestedClassUID);

		dicom::CommandSet::CGetRQ getRequestedInstanceUID(174,classUID);
		getRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.174"));
		assertGetRequestRejected(getRequestedInstanceUID);
		assertLegacyGetRequestRejected(getRequestedInstanceUID);

		dicom::CommandSet::CGetRQ getAffectedInstanceUID(186,classUID);
		getAffectedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.186"));
		assertGetRequestRejected(getAffectedInstanceUID);
		assertLegacyGetRequestRejected(getAffectedInstanceUID);

		dicom::CommandSet::CGetRQ getMoveDestination(202,classUID);
		getMoveDestination.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		assertGetRequestRejected(getMoveDestination);
		assertLegacyGetRequestRejected(getMoveDestination);

		dicom::CommandSet::CGetRQ getMoveOriginatorAET(204,classUID);
		getMoveOriginatorAET.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
		assertGetRequestRejected(getMoveOriginatorAET);
		assertLegacyGetRequestRejected(getMoveOriginatorAET);

		dicom::CommandSet::CGetRQ getMoveOriginatorMessageID(206,classUID);
		getMoveOriginatorMessageID.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(206));
		assertGetRequestRejected(getMoveOriginatorMessageID);
		assertLegacyGetRequestRejected(getMoveOriginatorMessageID);

		dicom::CommandSet::CMoveRQ duplicateMoveCommandField(98,classUID,"ARCHIVE_AE");
		duplicateMoveCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_MOVE_RQ);
		assertMoveRequestRejected(duplicateMoveCommandField);
		assertLegacyMoveRequestRejected(duplicateMoveCommandField);

		dicom::CommandSet::CMoveRQ duplicateMoveSOPClassUID(100,classUID,"ARCHIVE_AE");
		duplicateMoveSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID,classUID);
		assertMoveRequestRejected(duplicateMoveSOPClassUID);
		assertLegacyMoveRequestRejected(duplicateMoveSOPClassUID);

		dicom::CommandSet::CMoveRQ duplicateMoveMessageID(102,classUID,"ARCHIVE_AE");
		duplicateMoveMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID,UINT16(102));
		assertMoveRequestRejected(duplicateMoveMessageID);
		assertLegacyMoveRequestRejected(duplicateMoveMessageID);

		dicom::CommandSet::CMoveRQ duplicateMoveDataSetType(104,classUID,"ARCHIVE_AE");
		duplicateMoveDataSetType.Put<dicom::VR_US>(
			dicom::TAG_DATA_SET_TYPE,
			dicom::DataSetStatus::YES_DATA_SET);
		assertMoveRequestRejected(duplicateMoveDataSetType);
		assertLegacyMoveRequestRejected(duplicateMoveDataSetType);

		dicom::CommandSet::CMoveRQ missingMoveCommandField(130,classUID,"ARCHIVE_AE");
		missingMoveCommandField.erase(dicom::TAG_CMD_FIELD);
		assertMoveRequestRejected(missingMoveCommandField);
		assertLegacyMoveRequestRejected(missingMoveCommandField);

		dicom::CommandSet::CMoveRQ missingMoveSOPClassUID(132,classUID,"ARCHIVE_AE");
		missingMoveSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertMoveRequestRejected(missingMoveSOPClassUID);
		assertLegacyMoveRequestRejected(missingMoveSOPClassUID);

		dicom::CommandSet::CMoveRQ missingMoveMessageID(134,classUID,"ARCHIVE_AE");
		missingMoveMessageID.erase(dicom::TAG_MSG_ID);
		assertMoveRequestRejected(missingMoveMessageID);
		assertLegacyMoveRequestRejected(missingMoveMessageID);

		dicom::CommandSet::CMoveRQ missingMoveDataSetType(136,classUID,"ARCHIVE_AE");
		missingMoveDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertMoveRequestRejected(missingMoveDataSetType);
		assertLegacyMoveRequestRejected(missingMoveDataSetType);

		dicom::CommandSet::CMoveRQ wrongMoveCommandField(156,classUID,"ARCHIVE_AE");
		wrongMoveCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongMoveCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_GET_RQ);
		assertMoveRequestRejected(wrongMoveCommandField);
		assertLegacyMoveRequestRejected(wrongMoveCommandField);

		dicom::CommandSet::CMoveRQ wrongMoveSOPClassUID(158,wrongClassUID,"ARCHIVE_AE");
		assertMoveRequestRejected(wrongMoveSOPClassUID);
		assertLegacyMoveRequestRejected(wrongMoveSOPClassUID);

		dicom::CommandSet::CMoveRQ moveRequestedClassUID(176,classUID,"ARCHIVE_AE");
		moveRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			classUID);
		assertMoveRequestRejected(moveRequestedClassUID);
		assertLegacyMoveRequestRejected(moveRequestedClassUID);

		dicom::CommandSet::CMoveRQ moveRequestedInstanceUID(178,classUID,"ARCHIVE_AE");
		moveRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.178"));
		assertMoveRequestRejected(moveRequestedInstanceUID);
		assertLegacyMoveRequestRejected(moveRequestedInstanceUID);

		dicom::CommandSet::CMoveRQ moveAffectedInstanceUID(188,classUID,"ARCHIVE_AE");
		moveAffectedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.188"));
		assertMoveRequestRejected(moveAffectedInstanceUID);
		assertLegacyMoveRequestRejected(moveAffectedInstanceUID);

		dicom::CommandSet::CMoveRQ moveOriginatorAET(208,classUID,"ARCHIVE_AE");
		moveOriginatorAET.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
		assertMoveRequestRejected(moveOriginatorAET);
		assertLegacyMoveRequestRejected(moveOriginatorAET);

		dicom::CommandSet::CMoveRQ moveOriginatorMessageID(210,classUID,"ARCHIVE_AE");
		moveOriginatorMessageID.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(210));
		assertMoveRequestRejected(moveOriginatorMessageID);
		assertLegacyMoveRequestRejected(moveOriginatorMessageID);

		dicom::CommandSet::CFindRQ duplicateCommandField(58,classUID);
		duplicateCommandField.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_FIND_RQ);
		assertFindRequestRejected(duplicateCommandField);

		dicom::CommandSet::CFindRQ duplicateSOPClassUID(60,classUID);
		duplicateSOPClassUID.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		assertFindRequestRejected(duplicateSOPClassUID);

		dicom::CommandSet::CFindRQ duplicateMessageID(62,classUID);
		duplicateMessageID.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(62));
		assertFindRequestRejected(duplicateMessageID);

		dicom::CommandSet::CFindRQ duplicateDataSetType(64,classUID);
		duplicateDataSetType.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertFindRequestRejected(duplicateDataSetType);

		dicom::CommandSet::CFindRQ missingFindCommandField(138,classUID);
		missingFindCommandField.erase(dicom::TAG_CMD_FIELD);
		assertFindRequestRejected(missingFindCommandField);

		dicom::CommandSet::CFindRQ missingFindSOPClassUID(140,classUID);
		missingFindSOPClassUID.erase(dicom::TAG_AFF_SOP_CLASS_UID);
		assertFindRequestRejected(missingFindSOPClassUID);

		dicom::CommandSet::CFindRQ missingFindMessageID(142,classUID);
		missingFindMessageID.erase(dicom::TAG_MSG_ID);
		assertFindRequestRejected(missingFindMessageID);

		dicom::CommandSet::CFindRQ missingFindDataSetType(144,classUID);
		missingFindDataSetType.erase(dicom::TAG_DATA_SET_TYPE);
		assertFindRequestRejected(missingFindDataSetType);

		dicom::CommandSet::CFindRQ wrongFindCommandField(160,classUID);
		wrongFindCommandField.erase(dicom::TAG_CMD_FIELD);
		wrongFindCommandField.Put<dicom::VR_US>(
			dicom::TAG_CMD_FIELD,
			dicom::Command::C_MOVE_RQ);
		assertFindRequestRejected(wrongFindCommandField);

		dicom::CommandSet::CFindRQ wrongFindSOPClassUID(162,wrongClassUID);
		assertFindRequestRejected(wrongFindSOPClassUID);

		dicom::CommandSet::CFindRQ findRequestedClassUID(180,classUID);
		findRequestedClassUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_CLASS_UID,
			classUID);
		assertFindRequestRejected(findRequestedClassUID);

		dicom::CommandSet::CFindRQ findRequestedInstanceUID(182,classUID);
		findRequestedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_REQ_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.182"));
		assertFindRequestRejected(findRequestedInstanceUID);

		dicom::CommandSet::CFindRQ findAffectedInstanceUID(190,classUID);
		findAffectedInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.190"));
		assertFindRequestRejected(findAffectedInstanceUID);

		dicom::CommandSet::CFindRQ findMoveDestination(212,classUID);
		findMoveDestination.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		assertFindRequestRejected(findMoveDestination);

		dicom::CommandSet::CFindRQ findMoveOriginatorAET(214,classUID);
		findMoveOriginatorAET.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("MOVE_AE"));
		assertFindRequestRejected(findMoveOriginatorAET);

		dicom::CommandSet::CFindRQ findMoveOriginatorMessageID(216,classUID);
		findMoveOriginatorMessageID.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(216));
		assertFindRequestRejected(findMoveOriginatorMessageID);

		dicom::CommandSet::CFindRQ findWithDataElement(218,classUID);
		findWithDataElement.Put<dicom::VR_LO>(dicom::TAG_PAT_ID,std::string("PATIENT"));
		assertFindRequestRejected(findWithDataElement);

		dicom::CommandSet::CFindRQ findWithMessageIDBeingRespondedTo(219,classUID);
		findWithMessageIDBeingRespondedTo.Put<dicom::VR_US>(
			dicom::TAG_MSG_ID_RSP,
			UINT16(219));
		assertFindRequestRejected(findWithMessageIDBeingRespondedTo);

		dicom::DataSet invalidStorePriority;
		invalidStorePriority.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_CLASS_UID,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		invalidStorePriority.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.2"));
		invalidStorePriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_STORE_RQ);
		invalidStorePriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(26));
		invalidStorePriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, UINT16(0xffff));
		invalidStorePriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertStoreRequestRejected(invalidStorePriority);

		dicom::DataSet missingStorePriority;
		missingStorePriority.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_CLASS_UID,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		missingStorePriority.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.3"));
		missingStorePriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_STORE_RQ);
		missingStorePriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(42));
		missingStorePriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertStoreRequestRejected(missingStorePriority);

		dicom::CommandSet::CStoreRQ duplicateStorePriority(
			44,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.4"));
		duplicateStorePriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::HIGH);
		assertStoreRequestRejected(duplicateStorePriority);

		dicom::DataSet missingStoreInstanceUID;
		missingStoreInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_CLASS_UID,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		missingStoreInstanceUID.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_STORE_RQ);
		missingStoreInstanceUID.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(66));
		missingStoreInstanceUID.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		missingStoreInstanceUID.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertStoreRequestRejected(missingStoreInstanceUID);

		dicom::CommandSet::CStoreRQ duplicateStoreInstanceUID(
			68,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.5"));
		duplicateStoreInstanceUID.Put<dicom::VR_UI>(
			dicom::TAG_AFF_SOP_INST_UID,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.6"));
		assertStoreRequestRejected(duplicateStoreInstanceUID);

		dicom::CommandSet::CStoreRQ emptyStoreInstanceUID(
			69,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID(""));
		assertStoreRequestRejected(emptyStoreInstanceUID);

		dicom::CommandSet::CStoreRQ duplicateMoveOriginatorAET(
			70,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.7"),
			std::string("SCU_AE"),
			UINT16(70));
		duplicateMoveOriginatorAET.Put<dicom::VR_AE>(dicom::TAG_MOVE_ORIG_AET, std::string("SECOND_AE"));
		assertStoreRequestRejected(duplicateMoveOriginatorAET);

		dicom::CommandSet::CStoreRQ duplicateMoveOriginatorMessageID(
			72,
			dicom::CT_IMAGE_STORAGE_SOP_CLASS,
			dicom::UID("1.2.826.0.1.3680043.10.1553.13.8"),
			std::string("SCU_AE"),
			UINT16(72));
		duplicateMoveOriginatorMessageID.Put<dicom::VR_US>(dicom::TAG_MOVE_ORIG_MSG_ID, UINT16(73));
		assertStoreRequestRejected(duplicateMoveOriginatorMessageID);

		dicom::DataSet invalidFindPriority;
		invalidFindPriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		invalidFindPriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_FIND_RQ);
		invalidFindPriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(28));
		invalidFindPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, UINT16(0xffff));
		invalidFindPriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertFindRequestRejected(invalidFindPriority);

		dicom::DataSet missingFindPriority;
		missingFindPriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		missingFindPriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_FIND_RQ);
		missingFindPriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(46));
		missingFindPriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertFindRequestRejected(missingFindPriority);

		dicom::CommandSet::CFindRQ duplicateFindPriority(48,classUID);
		duplicateFindPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::HIGH);
		assertFindRequestRejected(duplicateFindPriority);

		dicom::DataSet invalidGetPriority;
		invalidGetPriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		invalidGetPriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_GET_RQ);
		invalidGetPriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(30));
		invalidGetPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, UINT16(0xffff));
		invalidGetPriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertGetRequestRejected(invalidGetPriority);

		dicom::DataSet missingGetPriority;
		missingGetPriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		missingGetPriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_GET_RQ);
		missingGetPriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(50));
		missingGetPriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertGetRequestRejected(missingGetPriority);

		dicom::CommandSet::CGetRQ duplicateGetPriority(52,classUID);
		duplicateGetPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::HIGH);
		assertGetRequestRejected(duplicateGetPriority);

		dicom::DataSet invalidLegacyGetPriority;
		invalidLegacyGetPriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		invalidLegacyGetPriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_GET_RQ);
		invalidLegacyGetPriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(32));
		invalidLegacyGetPriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, UINT16(0xffff));
		invalidLegacyGetPriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertLegacyGetRequestRejected(invalidLegacyGetPriority);
		assertLegacyGetRequestRejected(missingGetPriority);
		assertLegacyGetRequestRejected(duplicateGetPriority);

		dicom::DataSet invalidMovePriority;
		invalidMovePriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		invalidMovePriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RQ);
		invalidMovePriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(34));
		invalidMovePriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, UINT16(0xffff));
		invalidMovePriority.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		invalidMovePriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertMoveRequestRejected(invalidMovePriority);

		dicom::DataSet missingMovePriority;
		missingMovePriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		missingMovePriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RQ);
		missingMovePriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(54));
		missingMovePriority.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		missingMovePriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertMoveRequestRejected(missingMovePriority);

		dicom::CommandSet::CMoveRQ duplicateMovePriority(56,classUID,"ARCHIVE_AE");
		duplicateMovePriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::HIGH);
		assertMoveRequestRejected(duplicateMovePriority);

		dicom::DataSet invalidLegacyMovePriority;
		invalidLegacyMovePriority.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		invalidLegacyMovePriority.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RQ);
		invalidLegacyMovePriority.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(36));
		invalidLegacyMovePriority.Put<dicom::VR_US>(dicom::TAG_PRIORITY, UINT16(0xffff));
		invalidLegacyMovePriority.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("ARCHIVE_AE"));
		invalidLegacyMovePriority.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertLegacyMoveRequestRejected(invalidLegacyMovePriority);
		assertLegacyMoveRequestRejected(missingMovePriority);
		assertLegacyMoveRequestRejected(duplicateMovePriority);

		dicom::DataSet missingMoveDestination;
		missingMoveDestination.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		missingMoveDestination.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RQ);
		missingMoveDestination.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(38));
		missingMoveDestination.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		missingMoveDestination.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertMoveRequestRejected(missingMoveDestination);

		dicom::DataSet missingLegacyMoveDestination;
		missingLegacyMoveDestination.Put<dicom::VR_UI>(dicom::TAG_AFF_SOP_CLASS_UID, classUID);
		missingLegacyMoveDestination.Put<dicom::VR_US>(dicom::TAG_CMD_FIELD, dicom::Command::C_MOVE_RQ);
		missingLegacyMoveDestination.Put<dicom::VR_US>(dicom::TAG_MSG_ID, UINT16(40));
		missingLegacyMoveDestination.Put<dicom::VR_US>(dicom::TAG_PRIORITY, dicom::Priority::MEDIUM);
		missingLegacyMoveDestination.Put<dicom::VR_US>(dicom::TAG_DATA_SET_TYPE, dicom::DataSetStatus::YES_DATA_SET);
		assertLegacyMoveRequestRejected(missingLegacyMoveDestination);

		dicom::CommandSet::CMoveRQ duplicateMoveDestination(58,classUID,"ARCHIVE_AE");
		duplicateMoveDestination.Put<dicom::VR_AE>(dicom::TAG_MOVE_DEST, std::string("SECOND_AE"));
		assertMoveRequestRejected(duplicateMoveDestination);
		assertLegacyMoveRequestRejected(duplicateMoveDestination);

		dicom::CommandSet::CMoveRQ emptyMoveDestination(60,classUID,"");
		assertMoveRequestRejected(emptyMoveDestination);
		assertLegacyMoveRequestRejected(emptyMoveDestination);

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

			dicom::CommandSet::CGetRQ request(43,classUID);
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			dicom::HandleCGet(
				dicom::CGetStatusFunction(
					[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
					{
						return dicom::CSubOperationResult(dicom::Status::WARNING,3,1,1,1);
					}),
				scpSide,
				readRequest,
				classUID);

			TestCGetSCU scu(requestorSide,classUID);
			scu.setLastMessageID(43);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == dicom::Status::WARNING);
			assert(!response.exists(dicom::TAG_NUM_REMAIN_SUBOP));
			assert(get<UINT16>(response,dicom::TAG_NUM_COMPL_SUBOP) == 1);
			assert(get<UINT16>(response,dicom::TAG_NUM_FAIL_SUBOP) == 1);
			assert(get<UINT16>(response,dicom::TAG_NUM_WARN_SUBOP) == 1);
			assert(data.empty());
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

			dicom::CommandSet::CMoveRQ request(45,classUID,"ARCHIVE_AE");
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			dicom::HandleCMove(
				dicom::CMoveStatusFunction(
					[](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet&)
					{
						return dicom::CSubOperationResult(0xa702,3,1,2,0);
					}),
				scpSide,
				readRequest,
				classUID);

			TestCMoveSCU scu(requestorSide,classUID);
			scu.setLastMessageID(45);
			UINT16 status = 0;
			dicom::DataSet response;
			dicom::DataSet data;
			scu.readRSP(status,response,data);
			assert(status == 0xa702);
			assert(!response.exists(dicom::TAG_NUM_REMAIN_SUBOP));
			assert(get<UINT16>(response,dicom::TAG_NUM_COMPL_SUBOP) == 1);
			assert(get<UINT16>(response,dicom::TAG_NUM_FAIL_SUBOP) == 2);
			assert(get<UINT16>(response,dicom::TAG_NUM_WARN_SUBOP) == 0);
			assert(data.empty());
		}

		{
			int sockets[2];
			makeSocketPair(sockets);
			PairedService requestorSide(sockets[0], classUID);
			PairedService scpSide(sockets[1], classUID);

			dicom::CommandSet::CGetRQ request(47,classUID);
			dicom::DataSet requestData;
			requestData.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL, std::string("STUDY"));
			requestorSide.WriteCommand(request,classUID);
			dicom::DataSet readRequest;
			requireRead(scpSide,readRequest);
			requestorSide.WriteDataSet(requestData,classUID);

			bool callbackCalled = false;
			dicom::CGetSCP scp(
				[&callbackCalled](dicom::ServiceBase&, const dicom::DataSet&, dicom::DataSet& identifier)
				{
					assert(get<std::string>(identifier,dicom::TAG_QR_LEVEL) == "STUDY");
					callbackCalled = true;
				});
			scp.handle(scpSide,readRequest,classUID);
			assert(callbackCalled);
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
		std::atomic<bool> nGetAttributeListResponseHandled(false);
		std::atomic<bool> nSetAttributeListResponseHandled(false);
		std::atomic<bool> nCreateAttributeListResponseHandled(false);

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
			dicom::NAttributeHandlerFunction([classUID,responseUID,&nGetAttributeListHandled,&nGetAttributeListResponseHandled](
				dicom::ServiceBase& service,
				const dicom::DataSet& command,
				const dicom::DataSet&,
				dicom::DataSet& responseData,
				std::vector<dicom::Tag>& responseAttributeList)
			{
				assert(service.HasNegotiatedRole(classUID));
				assert(service.CanActAsSCP(classUID));
				if(command.Values(dicom::TAG_ATTR_ID_LIST).empty())
				{
					responseAttributeList.push_back(dicom::TAG_PAT_NAME);
					responseAttributeList.push_back(dicom::TAG_PAT_ID);
					nGetAttributeListResponseHandled = true;
					return UINT16(0x0120);
				}
				assert(command.Values(dicom::TAG_ATTR_ID_LIST).size() == 2);
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, responseUID);
				nGetAttributeListHandled = true;
				return dicom::Status::SUCCESS;
			}));
		server.AddNSetHandler(
			classUID,
			dicom::NAttributeHandlerFunction([classUID,responseUID,&nSetAttributeListResponseHandled](
				dicom::ServiceBase& service,
				const dicom::DataSet&,
				const dicom::DataSet& requestData,
				dicom::DataSet& responseData,
				std::vector<dicom::Tag>& responseAttributeList)
			{
				assert(service.HasNegotiatedRole(classUID));
				assert(service.CanActAsSCP(classUID));
				if(!requestData.Values(dicom::TAG_PAT_ID).empty()
					&& get<std::string>(requestData,dicom::TAG_PAT_ID) == "NSET0120")
				{
					responseAttributeList.push_back(dicom::TAG_PAT_NAME);
					responseAttributeList.push_back(dicom::TAG_PAT_ID);
					nSetAttributeListResponseHandled = true;
					return UINT16(0x0120);
				}
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, responseUID);
				return dicom::Status::SUCCESS;
			}));
		server.AddNActionHandler(classUID,handler);
		server.AddNCreateHandler(
			classUID,
			dicom::NCreateAttributeHandlerFunction([classUID,responseUID,&nCreateAttributeListResponseHandled](
				dicom::ServiceBase& service,
				const dicom::DataSet&,
				const dicom::DataSet& requestData,
				dicom::UID& responseInstUID,
				dicom::DataSet& responseData,
				std::vector<dicom::Tag>& responseAttributeList)
			{
				assert(service.HasNegotiatedRole(classUID));
				assert(service.CanActAsSCP(classUID));
				if(!requestData.Values(dicom::TAG_PAT_ID).empty()
					&& get<std::string>(requestData,dicom::TAG_PAT_ID) == "NCREATE0120")
				{
					responseAttributeList.push_back(dicom::TAG_PAT_NAME);
					responseAttributeList.push_back(dicom::TAG_PAT_ID);
					nCreateAttributeListResponseHandled = true;
					return UINT16(0x0120);
				}
				if(responseInstUID.str().empty())
					responseInstUID = responseUID;
				responseData.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, responseUID);
				return dicom::Status::SUCCESS;
			}));
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

				std::vector<dicom::Tag> emptyAttrList;
				response.clear();
				data.clear();
				getSCU.writeRQ(instUID,emptyAttrList);
				getSCU.readRSP(status,response,data);
				assert(status == 0x0120);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_GET_RSP);
				assertTwoAttributeIdentifiers(response);
				assert(data.empty());

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

				dicom::DataSet setAttributeRequestData;
				setAttributeRequestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NSET0120"));
				response.clear();
				data.clear();
				setSCU.writeRQ(instUID,setAttributeRequestData);
				setSCU.readRSP(status,response,data);
				assert(status == 0x0120);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_SET_RSP);
				assertTwoAttributeIdentifiers(response);
				assert(data.empty());

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
				createSCU.writeRQ(requestData);
				createSCU.readRSP(status,response,data);
				assert(status == dicom::Status::SUCCESS);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
				assert(get<dicom::UID>(response, dicom::TAG_AFF_SOP_INST_UID) == responseUID);
				assert(get<dicom::UID>(data, dicom::TAG_SOP_INST_UID) == responseUID);

				dicom::DataSet createAttributeRequestData;
				createAttributeRequestData.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("NCREATE0120"));
				response.clear();
				data.clear();
				createSCU.writeRQ(instUID,createAttributeRequestData);
				createSCU.readRSP(status,response,data);
				assert(status == 0x0120);
				assert(get<UINT16>(response, dicom::TAG_CMD_FIELD) == dicom::Command::N_CREATE_RSP);
				assertTwoAttributeIdentifiers(response);
				assert(data.empty());

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
		assert(nGetAttributeListResponseHandled);
		assert(nSetAttributeListResponseHandled);
		assert(nCreateAttributeListResponseHandled);
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
				assert(!response.exists(dicom::TAG_NUM_REMAIN_SUBOP));
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
				assert(!response.exists(dicom::TAG_NUM_REMAIN_SUBOP));
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
				assert(!response.exists(dicom::TAG_NUM_REMAIN_SUBOP));
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
	checkImplementationIdentityDefaults();
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
	checkNdimseCommandFieldMultiplicityValidation();
	checkCdimseRoleEnforcement();
	checkCdimseAsynchronousOperationsWindowEnforcement();
	checkNdimseAsynchronousOperationsWindowEnforcement();
	checkCCancel();
	checkCCancelOverPData();
	checkSCUResponseValidationOverPData();
	checkRetrieveFailedSOPInstanceUIDListEmission();
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
