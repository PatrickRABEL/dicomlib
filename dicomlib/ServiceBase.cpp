/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/

#include <vector>
#include <iostream>
#include <stdexcept>
#include "ServiceBase.hpp"
//#include "pdata.hpp"
#include "Encoder.hpp"
#include "Decoder.hpp"
#include "aarj.hpp"
#include "aarq.hpp"
#include "iso646.h"

using std::cout;using std::endl;
using std::vector;

namespace dicom
{
	using namespace primitive;

	namespace
	{
		const primitive::SCPSCURoleSelect* FindRoleSelection(
			const std::vector<primitive::SCPSCURoleSelect>& roles,
			const UID& uid)
		{
			for(std::vector<primitive::SCPSCURoleSelect>::const_iterator I=roles.begin();I!=roles.end();++I)
				if(I->UID_==uid)
					return &(*I);
			return 0;
		}
	}

	ServiceBase::ServiceBase()
		:CurrentPresentationContextID_(0) //0 is not a valid number for Presentation Context ID -Sam
		,HasNegotiatedAsynchronousOperationsWindow_(false)
		,MaximumNumberOperationsInvoked_(1)
		,MaximumNumberOperationsPerformed_(1)
	{}

	//ServiceBase::ServiceBase(Network::Socket* socket):socket_(socket)
	//{
	//	//might want to do some validation here, e.g. check socket state?
	//}

	ServiceBase::~ServiceBase()
	{

	}

	void ServiceBase::Write(MessageControlHeader::Code msgHead, const DataSet& ds,
		const UID& AbstractSyntaxUID, TS ts)
	{

		//UID absUID(as);
		UID tsUID(ts.getUID());

		//this maybe wrong -Sam Shen


        CurrentPresentationContextID_= GetPresentationContextID(AbstractSyntaxUID);
/*
		if(CurrentPresentationContextID_!=0)
			PresentationContextID=CurrentPresentationContextID_;
		else
			PresentationContextID = GetPresentationContextID(AbstractSyntaxUID);
        */

		//check that it's valid for the current transfer syntax.
		/*
			I've removed this check because this is used for sending commands, that are
			always Implicit/LittleEndian, even if that transfer syntax hasn't been negotiated.
		*/
		//if (!GetPresentationContextID(AbstractSyntaxUID, tsUID))
		//	throw exception("No presentation context ID");//bad transfer syntax, or something?

		int ByteOrder=ts.isBigEndian()?__BIG_ENDIAN:__LITTLE_ENDIAN;
		Buffer buffer(ByteOrder);

		dicom::WriteToBuffer(ds,buffer,ts);

		UINT32 MaxPDULength=AAssociateRQ_.UserInfo_.MaxSubLength_.MaximumLength_;
		Write(buffer,msgHead,/*PresentationContextID*/CurrentPresentationContextID_,MaxPDULength);
	}

	void ServiceBase::WriteCommand(const DataSet& ds,const UID& uid )
	{
		Write(MessageControlHeader::COMMAND, ds, uid, TS(IMPL_VR_LE_TRANSFER_SYNTAX)/*::IMPL_VR_LE*/);//Commands MUST have VR/LE Transfer Syntax.
	}

	void ServiceBase::WriteDataSet(const dicom::DataSet& ds, const UID& uid/*, TS ts*/)
	{
		TS ts = GetTransferSyntaxUID(CurrentPresentationContextID_);
		Write(MessageControlHeader::DATASET, ds, uid, ts);
	}

	/*!
		This functionality used to be in PDATATF
	*/
	void ServiceBase::Write(Buffer& buffer,MessageControlHeader::Code msgHead,BYTE PresentationContextID,UINT32 MaxPDULength)
	{
		//slice buffer and send as a series of P_DATA_TF thingies

		//each P_DATA_TF shall have only one PDV, to make life easier.

		//buffer.reset();

		if(buffer.position()!=buffer.begin())
		{
			throw exception("buffer.position()!=buffer.begin(), in ServiceBase::Write()");
		}

		if(MaxPDULength==0)//Does this ever happen?  Is this valid to do?  Must have
							//rationale here please.   TODO.
			MaxPDULength=static_cast<UINT32>(buffer.size());


		Network::Socket* socket=GetSocket();

		while(buffer.position()!=buffer.end())
		{

		/*
			See Part 8, table 9-22 and 9-23 to understand what we're sending here,
			and realise that I've opted to only ever send ONE pdv with each P-DATA-TF.
		*/
			*socket << BYTE(0x04);
			*socket << BYTE(0x00);
			
			UINT32 BytesLeftToSend=static_cast<UINT32>((buffer.end()-buffer.position()));
			
			const UINT32 BytesInThisChunk=std::min<UINT32>(BytesLeftToSend,MaxPDULength-6);


			*socket << UINT32(BytesInThisChunk+6);
			//now the PDV item
			//We should use the PDV class to mediate this!

			*socket << UINT32(BytesInThisChunk+2);
			*socket << PresentationContextID;


			if(buffer.position()+(BytesInThisChunk)==buffer.end())
				msgHead |=MessageControlHeader::LAST_FRAGMENT;

			*socket	<< msgHead;

			//then send data...
			BYTE* Begin=&(*(buffer.position()));
			socket->Sendn(Begin,(BytesInThisChunk));

			//buffer.position()+=(BytesInThisChunk);//this is now a bug...
			buffer.Increment(BytesInThisChunk);
		}
	}

	/*!
		returns false on association termination, else true.


		(We really shouldn't _handle_ the termination here, functions should do
		one and only one thing.

		This needs to be re-thought - it's really ugly...

	*/
	/*
	This function needs re-work. When servicebase,i.e. pdu read a dataset, it does 
	not know ahead what transfersyntax it will be. It is not defined in the command
	dataset. It is defined in the PresID in p_data_tf header. In principle(e.g. dcmtk), 
	one AbstractSyntax(SOPClass) can support multiple TS. In other words, we will 
	have to determine the ts on-the-fly. Or you can only support one ts, i.e. 
	implicitVR_littleEndian. That is why each time I try to support ExpliciVR
	I have a problem. 
	
	I change the interface of this function, removing the ts parameter. In theory,
	a p_data_tf can contain more than one dataset due to the multiplicity of pdv items.
	But in reality, because 1. the pdu's MaxSubSize is merely 16384, 2. nobody is doing that
	p_data_tf contains only one dataset, either a command or a dataset
	-Sam Shen Jan 29, 2007
	*/
	bool ServiceBase::Read(DataSet& command_or_data)
	{
		//Specified at Part 8/figure 9-2
		/*
			What we basically do here is pull data off of the TCP/IP stream onto
			a PDataTF object(which has a BUFFER), and then at the end read
			from the buffer onto the DataSet.  I'm not entirely happy about this
			design, but there doesn't seem to be any other feasible approach.
		*/

		Network::Socket* socket=GetSocket();
		Buffer p_data_tf_buffer;
		MessageControlHeader::Code msgHead;
		while(true)//loop, apparently implying that we can expect more than one PDATATF object.
		{
			BYTE		ItemType;
			*socket >> ItemType;
			switch ( ItemType )//shouldn't be 1, 2 or 3.
			{
			case	0x04:	// P-DATA-TF
				{
					bool ready_to_parse=false;
					ReadDynamic(*socket,p_data_tf_buffer,msgHead,ready_to_parse);
					if (ready_to_parse)//what is the corresponding 'else' ?
					{
						ParseRawVRIntoDataSet(p_data_tf_buffer,msgHead,command_or_data);
						return true;
					}
					else
						break;	//keep going through loop
					
				}
				break;
			case	0x05:	// A-RELEASE-RQ
				{
					AReleaseRQ release_request;
					release_request.ReadDynamic(*socket);

					// also drop
					AReleaseRP release_response;
					release_response.Write(*socket);
					return false;
				}
				// 			case	0x06:	// A-RELEASE-RP//shouldn't happen for a server

			case	0x07:	// A-ABORT-RQ
				{
					//shouldn't we try to read the abort request here
					AAbortRQ abort_request(*socket);
					throw AssociationAborted(abort_request);
				}
			default:
				{
					AAbortRQ abort_request(AAbortRQ::DICOM_SERVICE_PROVIDER,AAbortRQ::UNRECOGNIZED_PDU);
					abort_request.Write (*socket);
					throw BadItemType(ItemType,0);
				}
			}
		}

	}

	/*
	This function reads the socket and strip off the pdu/pdv fields and put the dataset (command
	or data)into a buffer for later parsing. It is from the previous class PDataTF -Sam
	*/
	void ServiceBase::ReadDynamic(Network::Socket& socket,Buffer& p_data_tf_buffer,MessageControlHeader::Code& msgHead,bool& ready_to_parse)
	{
 		UINT32		Count;
		//1. Read in the pdu fields
		//BYTE pdu_type has been read before entering this function
		BYTE pdu_reserve;
 		socket >> pdu_reserve;
		UINT32 pdu_length;
 		socket >> pdu_length;

 		Count = pdu_length;
 		while ( Count > 0)
 		{
			UINT32 pdv_item_length;
 			socket >> pdv_item_length;
 			socket >> CurrentPresentationContextID_;
 			socket >> msgHead;


			//now read actual data from socket onto buffer.
			//std::vector<BYTE> data(pdv.Length-2);
			//socket >> data;
			//std::copy(data.begin(),data.end(),std::back_inserter(buffer_));
			p_data_tf_buffer.insert(p_data_tf_buffer.end(),pdv_item_length-2,0x00);
			socket.Readn(&*(p_data_tf_buffer.end()-(pdv_item_length-2)),(pdv_item_length-2));//slightly more complicated.


			/*
				The previous 3 lines could be dramatically speeded up if
				we _first_ allocate the space on buffer, then pass _pointers_
				to the begin and end of the new space to Socket::Readn();

				I'm not going to change this till I have a good unit test for the change

				Meyers says in 'Efficient STL' that member functions are preferable
				over algorithms for efficiency?

				So possible optimizations would be:
				1)

				buffer_.insert(buffer_.end(),data.begin(),data.end());//this is simple.

				2)

				buffer_.insert(buffer_.end(),pdv.Length-2,0x00);
				socket.Readn(buffer_.end()-(pdv.Length-2),(pdv.Length-2));//slightly more complicated.

				We need profiling tests for both, such as dicomtest::SubmitLotsOfImages()
			*/

 			Count = Count - pdv_item_length - sizeof(UINT32);
 			//Length = Length - pdv_item_length - sizeof(UINT32);

			if((msgHead bitand MessageControlHeader::LAST_FRAGMENT)!=0)
 			{
 				ready_to_parse = true;
 				return;
 			}
 		}
		//if((pdv_message_control_header bitand MessageControlHeader::LAST_FRAGMENT)!=0)
 	//	{//what is this for? -Sam
 	//		assert(0);//how can this ever happen?
		//	ready_to_parse = true;
 	//		return;
 	//	}
 		return;
 	}
		void ServiceBase::ParseRawVRIntoDataSet(Buffer& p_data_tf_buffer,const MessageControlHeader::Code& msgHead, DataSet& command_or_data)
		{
		//first thing: determine the endian of the buffer

		if(p_data_tf_buffer.position()!=p_data_tf_buffer.begin())
		{
			throw exception("buffer.position()!=buffer.begin(), in ServiceBase::ParseRawVRIntoDataSet()");
		}
		//remember: command dataset is always little endian
		UID tsuid;
		if(not(msgHead bitand MessageControlHeader::COMMAND))
			tsuid=GetTransferSyntaxUID(CurrentPresentationContextID_);
		else
			tsuid=IMPL_VR_LE_TRANSFER_SYNTAX;
		ReadFromBuffer(p_data_tf_buffer,command_or_data,TS(tsuid));//defined in decoder.cpp
	}

	/*
		I'm still not sure about the following two functions.
	*/



	/*
		return the ID member of the first PresentationContext in
		AAssociateRQ_.ProposedPresentationContexts_ whose AbsSyntax.UID_ matches uid.
	*/
		/*
		I find it necessary to check both AbstractSyntaxUID and TransferSyntax UID when
		test with dcmtk's storescu.exe. Storescu.exe negotiates with preferred ts over 
		default(ImplicitLittleEndian) in the PCs. The preferred ts and default belong to 
		two PresentationContextIDs. When not checked with ts, we have bug reporting the 
		wrong PresentationContextID, of which we don't support the transfersyntax. -Sam

		*/

	BYTE ServiceBase::GetPresentationContextID(const UID& uid)
	{
		//shouldn't we just be interested in accepted presentation contexts?-Trevor
		//But AcceptedPresentationContexts_ does not contain UID. -Sam
		const vector<PresentationContext>&	PCArray = AAssociateRQ_.ProposedPresentationContexts_;
		//const vector<PresentationContext>& PCArray=AcceptedPresentationContexts_;
		
		
		size_t Index = 0;
		while ( Index < PCArray.size())
		{
			const PresentationContext& PresContext = PCArray.at ( Index );
			const PresentationContextAccept& APresContext = AcceptedPresentationContexts_.at( Index );
			if(PresContext.AbsSyntax_.UID_ == uid && APresContext.Result_==0)//The first accepted PresID ever found -Sam
				return (PresContext.ID_);
			++Index;
		}
		//	we could replace above with a call to find_if ???


		throw dicom::exception("Couldn't get Presentation Context ID");	// You're probably trying to use a SOP class that
																		// wasn't negotiated during association!

	}

	/*!
		Get the PCID for a given AbsUID and TrnUID
	*/

	BYTE ServiceBase::GetPresentationContextID(const UID& AbsUID, const UID& TrnUID)
	{
		size_t Index = 0;

		//shouldn't we just be interested in accepted presentation contexts?
		
		while ( Index < AAssociateRQ_.ProposedPresentationContexts_.size())
		{
			PresentationContext	PresContext = AAssociateRQ_.ProposedPresentationContexts_.at ( Index );
			
			if(PresContext.AbsSyntax_.UID_ == AbsUID)
			{
				PresentationContextAccept	PCA;
				size_t Index = 0;
				while (Index < AcceptedPresentationContexts_.size() )
				{

					PCA = AcceptedPresentationContexts_.at ( Index );
					if(PCA.TrnSyntax_.UID_ == TrnUID &&
						PCA.PresentationContextID_ == PresContext.ID_)
					{
						return ( PCA.PresentationContextID_);
					}
					++Index;
				}
			}
			++Index;
		}
		throw dicom::exception("given presentation context does not exist with specified transfer syntax.");
	}

	//BYTE ServiceBase::GetPresentationContextID(const UID& AbsUID, const UID& TrnUID)
	//{
	//	struct match
	//	{
	//		match(const UID& a, const UID& t):a_(a),t_(t){}
	//		bool operator()(AcceptedPresentationContext a)
	//		{
	//			return (a.first.UID_==a_ && a.second.UID_==t_);
	//		}
	//	};
	//	std::vector<AcceptedPresentationContext>::const_iterator I =
	//		std::find_if(AcceptedPresentationContexts_.begin(),AcceptedPresentationContexts_.end()
	//		match(AbsUID,TrnUID));
	//	if(I!=AcceptedPresentationContexts_.end())
	//		return 
	//	/*
	//		search 	AcceptedPresentationContexts_
	//		for matching pair.
	//	*/
	//	throw dicom::exception("given presentation context does not exist with specified transfer syntax.");
	//}

	


	UID ServiceBase::GetTransferSyntaxUID(BYTE PresentationContextID)
	{
		typedef std::vector<primitive::PresentationContextAccept>::const_iterator Iter;

		for(Iter I=AcceptedPresentationContexts_.begin();I!=AcceptedPresentationContexts_.end();I++)
			if(I->PresentationContextID_==PresentationContextID)
				return I->TrnSyntax_.UID_;
		throw std::runtime_error("Couldn't identify Presentation Context");
	}

	void ServiceBase::RequestCancel(UINT16 messageID)
	{
		CancelRequestedMessageIDs_.insert(messageID);
	}

	bool ServiceBase::IsCancelRequested(UINT16 messageID) const
	{
		return CancelRequestedMessageIDs_.find(messageID) != CancelRequestedMessageIDs_.end();
	}

	void ServiceBase::ClearCancelRequest(UINT16 messageID)
	{
		CancelRequestedMessageIDs_.erase(messageID);
	}

	ServiceBase::AssociationRole::AssociationRole()
		:SCU_(false)
		,SCP_(false)
	{
	}

	ServiceBase::AssociationRole::AssociationRole(bool scu, bool scp)
		:SCU_(scu)
		,SCP_(scp)
	{
	}

	void ServiceBase::ClearNegotiatedAssociationOptions()
	{
		NegotiatedRoles_.clear();
		NegotiatedSOPClassExtendedInformation_.clear();
		HasNegotiatedAsynchronousOperationsWindow_ = false;
		MaximumNumberOperationsInvoked_ = 1;
		MaximumNumberOperationsPerformed_ = 1;
	}

	void ServiceBase::ApplyAssociationNegotiationAsRequestor(
		const primitive::AAssociateAC& acknowledgement)
	{
		ClearNegotiatedAssociationOptions();
		for(size_t Index=0; Index<AAssociateRQ_.ProposedPresentationContexts_.size(); ++Index)
		{
			const primitive::PresentationContext& context =
				AAssociateRQ_.ProposedPresentationContexts_.at(Index);
			bool accepted = false;
			for(size_t AcceptedIndex=0; AcceptedIndex<acknowledgement.PresContextAccepts_.size(); ++AcceptedIndex)
			{
				const primitive::PresentationContextAccept& acceptedContext =
					acknowledgement.PresContextAccepts_.at(AcceptedIndex);
				if(acceptedContext.PresentationContextID_==context.ID_ && acceptedContext.Result_==0)
				{
					accepted = true;
					break;
				}
			}
			if(!accepted)
				continue;

			const primitive::SCPSCURoleSelect* role =
				FindRoleSelection(acknowledgement.UserInfo_.SCPSCURoles_,context.AbsSyntax_.UID_);
			if(role)
				NegotiatedRoles_[context.AbsSyntax_.UID_] =
					AssociationRole(role->SCURole_!=0,role->SCPRole_!=0);
			else
				NegotiatedRoles_[context.AbsSyntax_.UID_] = AssociationRole(true,false);
		}
		if(acknowledgement.UserInfo_.HasAsynchronousOperationsWindow_)
		{
			HasNegotiatedAsynchronousOperationsWindow_ = true;
			MaximumNumberOperationsInvoked_ =
				acknowledgement.UserInfo_.AsyncOperationsWindow_.MaximumNumberOperationsInvoked_;
			MaximumNumberOperationsPerformed_ =
				acknowledgement.UserInfo_.AsyncOperationsWindow_.MaximumNumberOperationsPerformed_;
		}
		for(size_t Index=0; Index<acknowledgement.UserInfo_.SOPClassExtendedNegotiations_.size(); ++Index)
		{
			const primitive::SOPClassExtendedNegotiation& negotiation =
				acknowledgement.UserInfo_.SOPClassExtendedNegotiations_.at(Index);
			NegotiatedSOPClassExtendedInformation_[negotiation.UID_] =
				negotiation.ServiceClassApplicationInformation_;
		}
	}

	void ServiceBase::ApplyAssociationNegotiationAsAcceptor(
		const primitive::UserInformation& acceptedUserInfo)
	{
		ClearNegotiatedAssociationOptions();
		for(size_t Index=0; Index<AAssociateRQ_.ProposedPresentationContexts_.size(); ++Index)
		{
			const primitive::PresentationContext& context =
				AAssociateRQ_.ProposedPresentationContexts_.at(Index);
			if(Index>=AcceptedPresentationContexts_.size() ||
				AcceptedPresentationContexts_.at(Index).Result_!=0)
				continue;

			const primitive::SCPSCURoleSelect* role =
				FindRoleSelection(acceptedUserInfo.SCPSCURoles_,context.AbsSyntax_.UID_);
			if(role)
				NegotiatedRoles_[context.AbsSyntax_.UID_] =
					AssociationRole(role->SCPRole_!=0,role->SCURole_!=0);
			else
				NegotiatedRoles_[context.AbsSyntax_.UID_] = AssociationRole(false,true);
		}
		if(acceptedUserInfo.HasAsynchronousOperationsWindow_)
		{
			HasNegotiatedAsynchronousOperationsWindow_ = true;
			MaximumNumberOperationsInvoked_ =
				acceptedUserInfo.AsyncOperationsWindow_.MaximumNumberOperationsInvoked_;
			MaximumNumberOperationsPerformed_ =
				acceptedUserInfo.AsyncOperationsWindow_.MaximumNumberOperationsPerformed_;
		}
		for(size_t Index=0; Index<acceptedUserInfo.SOPClassExtendedNegotiations_.size(); ++Index)
		{
			const primitive::SOPClassExtendedNegotiation& negotiation =
				acceptedUserInfo.SOPClassExtendedNegotiations_.at(Index);
			NegotiatedSOPClassExtendedInformation_[negotiation.UID_] =
				negotiation.ServiceClassApplicationInformation_;
		}
	}

	bool ServiceBase::HasNegotiatedRole(const UID& uid) const
	{
		return NegotiatedRoles_.find(uid)!=NegotiatedRoles_.end();
	}

	bool ServiceBase::CanActAsSCU(const UID& uid) const
	{
		std::map<UID,AssociationRole>::const_iterator I = NegotiatedRoles_.find(uid);
		return I!=NegotiatedRoles_.end() && I->second.SCU_;
	}

	bool ServiceBase::CanActAsSCP(const UID& uid) const
	{
		std::map<UID,AssociationRole>::const_iterator I = NegotiatedRoles_.find(uid);
		return I!=NegotiatedRoles_.end() && I->second.SCP_;
	}

	bool ServiceBase::HasNegotiatedAsynchronousOperationsWindow() const
	{
		return HasNegotiatedAsynchronousOperationsWindow_;
	}

	UINT16 ServiceBase::MaximumNumberOperationsInvoked() const
	{
		return MaximumNumberOperationsInvoked_;
	}

	UINT16 ServiceBase::MaximumNumberOperationsPerformed() const
	{
		return MaximumNumberOperationsPerformed_;
	}

	bool ServiceBase::HasNegotiatedSOPClassExtended(const UID& uid) const
	{
		return NegotiatedSOPClassExtendedInformation_.find(uid)!=
			NegotiatedSOPClassExtendedInformation_.end();
	}

	const std::vector<BYTE>& ServiceBase::GetNegotiatedSOPClassExtendedInformation(const UID& uid) const
	{
		std::map<UID,std::vector<BYTE> >::const_iterator I =
			NegotiatedSOPClassExtendedInformation_.find(uid);
		if(I==NegotiatedSOPClassExtendedInformation_.end())
			throw exception("SOP Class Extended Negotiation was not negotiated for SOP Class");
		return I->second;
	}

	//bool ServiceBase::GetTransferSyntaxUID(BYTE PCID, UID& uid)
	//{
	//	PresentationContextAccept	PCA;

	//	size_t	Index = 0;

	//	while (Index < AcceptedPresentationContexts_.size() )
	//	{
	//		PCA = AcceptedPresentationContexts_.at ( Index );
	//		if(PCA.PresentationContextID_ == PCID)
	//		{
	//			uid = PCA.TrnSyntax_.UID_;
	//			return ( true );
	//		}
	//		++Index;
	//	}
	//	return ( false );//should be a throw...
	//}
}//namespace dicom
