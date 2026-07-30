#ifndef SERVICE_BASE_HPP_23847239487238
#define SERVICE_BASE_HPP_23847239487238
#include <map>
#include <set>
#include <string>
#include <vector>
#include "socket/Socket.hpp"
#include "Buffer.hpp"
#include "DataSet.hpp"
#include "TransferSyntax.hpp"
#include "pdata.hpp"
#include "aaac.hpp"
#include "aarj.hpp"
#include "UIDs.hpp"

namespace dicom
{

	//namespace MessageControlHeader
	//{
	//	typedef BYTE Code;
	//	const Code	DATASET			= 0x00,
	//				COMMAND 		= 0x01,
	//				LAST_FRAGMENT	= 0x02;
	//}

	//!Thrown if connection is aborted.
	struct AssociationAborted : public dicom::exception
	{
		primitive::AAbortRQ abort_request_;
		AssociationAborted (const primitive::AAbortRQ& abort_request)
			: dicom::exception("Association Aborted"),
			abort_request_(abort_request)
		{}
	};

	//!Holds shared functionality for dicom client and server classes.
	/*!
		Manages reading and writing control messages and datasets to/from
		a socket.

		Keeps track of conditions under which association was set up
	*/
	struct ServiceBase
	{
		ServiceBase(const ServiceBase&) = delete;
		ServiceBase& operator=(const ServiceBase&) = delete;

		//ServiceBase(Network::Socket* socket);
		ServiceBase();

		virtual ~ServiceBase();


		void Write(MessageControlHeader::Code msgHead, const DataSet& ds,const UID& AbstractSyntaxUID, TS ts);

		void Write(Buffer& buffer,MessageControlHeader::Code msgHead,BYTE PresentationContextID,UINT32 MaxPDULength);

		void WriteCommand(const DataSet& ds,const UID& uid);

		void WriteDataSet(const dicom::DataSet& ds, const UID& uid/*, TS ts = TS(IMPL_VR_LE_TRANSFER_SYNTAX)*//*IMPL_VR_LE*/);
		//I comment out the default TS because this should be determined by PresID on the fly. -Sam
		void ReadDynamic(Network::Socket& socket,Buffer& p_data_tf_buffer,MessageControlHeader::Code& msgHead,bool& ready_to_parse);
		void ParseRawVRIntoDataSet(Buffer& p_data_tf_buffer,const MessageControlHeader::Code& msgHead,DataSet& command_or_data);

		bool Read(DataSet& command_or_data);

		BYTE GetPresentationContextID(const UID& uid);
		BYTE GetPresentationContextID(const UID& AbsUID,	const UID& TrnUID);
		//!Like GetPresentationContextID(uid), but returns 0 instead of throwing.
		/*!
			0 is not a valid Presentation Context ID, so it unambiguously means
			"this abstract syntax was not negotiated". Used where the absence of a
			context is an expected case rather than an error, e.g. a Meta SOP Class
			association (PS3.4 Annex H), where the child SOP Class named in the
			command set has no presentation context of its own.
		*/
		BYTE FindPresentationContextID(const UID& uid);

		//bool GetTransferSyntaxUID(BYTE, UID& TrnUID);

		UID GetTransferSyntaxUID(BYTE PresentationContextID);

		void RequestCancel(UINT16 messageID);
		bool HasCancelRequest() const;
		bool IsCancelRequested(UINT16 messageID) const;
		void ClearCancelRequest(UINT16 messageID);

		struct AssociationRole
		{
			bool SCU_;
			bool SCP_;

			AssociationRole();
			AssociationRole(bool scu, bool scp);
		};

		void ClearNegotiatedAssociationOptions();
		void ApplyAssociationNegotiationAsRequestor(const primitive::AAssociateAC& acknowledgement);
		void ApplyAssociationNegotiationAsAcceptor(const primitive::UserInformation& acceptedUserInfo);
		bool HasNegotiatedRole(const UID& uid) const;
		bool CanActAsSCU(const UID& uid) const;
		bool CanActAsSCP(const UID& uid) const;
		bool HasNegotiatedAsynchronousOperationsWindow() const;
		UINT16 MaximumNumberOperationsInvoked() const;
		UINT16 MaximumNumberOperationsPerformed() const;
		UINT16 OutstandingOperationsInvoked() const;
		UINT16 OutstandingOperationsPerformed() const;
		bool CanInvokeOperation() const;
		bool CanPerformOperation() const;
		bool IsInvokedOperationOutstanding(UINT16 messageID) const;
		bool IsPerformedOperationOutstanding(UINT16 messageID) const;
		void BeginInvokedOperation();
		void BeginInvokedOperation(UINT16 messageID);
		void CompleteInvokedOperation();
		void CompleteInvokedOperation(UINT16 messageID);
		void BeginPerformedOperation();
		void BeginPerformedOperation(UINT16 messageID);
		void CompletePerformedOperation();
		void CompletePerformedOperation(UINT16 messageID);
		bool HasNegotiatedSOPClassExtended(const UID& uid) const;
		const std::vector<BYTE>& GetNegotiatedSOPClassExtendedInformation(const UID& uid) const;


		//Following two parameters keep a record of the conditions under which
		//this services association was set up.

		//!The association we accepted.
		primitive::AAssociateRQ AAssociateRQ_;

		//!The presentation contexts we accepted.
		std::vector<primitive::PresentationContextAccept>	AcceptedPresentationContexts_;

		//!The current PresentationContextID we receive in the latest PDV
		/*
		This member does not belong to this place. It should belong  PDV. However, the whole
		data structure has been messed up. It is too hard to correct it. Let's put it here
		for now. -Sam Shen Jan 22, 2007
		*/
		BYTE CurrentPresentationContextID_;

		//!Message IDs referenced by valid C-CANCEL-RQ commands on this association.
		std::set<UINT16> CancelRequestedMessageIDs_;

		std::map<UID,AssociationRole> NegotiatedRoles_;
		bool HasNegotiatedAsynchronousOperationsWindow_;
		UINT16 MaximumNumberOperationsInvoked_;
		UINT16 MaximumNumberOperationsPerformed_;
		UINT16 OutstandingOperationsInvoked_;
		UINT16 OutstandingOperationsPerformed_;
		std::set<UINT16> OutstandingInvokedMessageIDs_;
		std::set<UINT16> OutstandingPerformedMessageIDs_;
		std::map<UID,std::vector<BYTE> > NegotiatedSOPClassExtendedInformation_;

		//this function should only be called on the client side because client decides which
		//transfer syntax to use. -Sam
		void SetCurrentPCID(BYTE pcid){CurrentPresentationContextID_=pcid;}
		//!The socket on which we're communicating
		/*!
			Currently this pointer is managed externally to this class, which
			is unfortunate.  I'd like to tighten this up.
			Should this be a pure virtual function rather than a member?

			TODO  make a pure virtual function
		*/

		virtual Network::Socket* GetSocket()=0;

		//Network::Socket* socket_;


	};
}//namespace dicom
#endif//SERVICE_BASE_CLASS_23847239487238
