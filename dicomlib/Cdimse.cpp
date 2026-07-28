/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/
#include <algorithm>
#include <iostream>
#include "Cdimse.hpp"
#include "ClientConnection.hpp"
#include "ServiceBase.hpp"

#include "Dumper.hpp"
/*
	This file needs a lot of cleaning up work.
*/
/*
	I'm a bit worried that the 'Group Length' field
	never seems to get inserted, as the standard seems
	to require for all messages.  Am I missing something
	here?
*/

using std::for_each;
using std::cout;
using std::endl;
using std::ios;
using std::find;
using std::string;

namespace dicom
{
	namespace
	{
		void ValidateCdimseResponse(
			const DataSet& response,
			Command::Code expectedCommand,
			UINT16 expectedMessageID,
			const UID& expectedClassUID,
			const UID* expectedInstanceUID = 0)
		{
			UINT16 command = 0;
			UINT16 responseMessageID = 0;
			response(TAG_CMD_FIELD) >> command;
			response(TAG_MSG_ID_RSP) >> responseMessageID;

			if(command != expectedCommand)
				throw exception("Unexpected C-DIMSE response command field");
			if(responseMessageID != expectedMessageID)
				throw exception("Unexpected C-DIMSE response message ID");
			if(response.exists(TAG_AFF_SOP_CLASS_UID))
			{
				UID responseClassUID;
				response(TAG_AFF_SOP_CLASS_UID) >> responseClassUID;
				if(responseClassUID != expectedClassUID)
					throw exception("Unexpected C-DIMSE response SOP Class UID");
			}
			if(expectedInstanceUID && response.exists(TAG_AFF_SOP_INST_UID))
			{
				UID responseInstanceUID;
				response(TAG_AFF_SOP_INST_UID) >> responseInstanceUID;
				if(responseInstanceUID != *expectedInstanceUID)
					throw exception("Unexpected C-DIMSE response SOP Instance UID");
			}
		}

		void ValidateCdimseResponseStatus(UINT16 status, Command::Code command)
		{
			bool valid = false;
			switch(command)
			{
			case Command::C_ECHO_RSP:
				valid = IsCEchoResponseStatus(status);
				break;
			case Command::C_STORE_RSP:
				valid = IsCStoreResponseStatus(status);
				break;
			case Command::C_FIND_RSP:
				valid = IsCFindResponseStatus(status);
				break;
			case Command::C_GET_RSP:
				valid = IsCGetResponseStatus(status);
				break;
			case Command::C_MOVE_RSP:
				valid = IsCMoveResponseStatus(status);
				break;
			default:
				throw exception("Unexpected C-DIMSE response command field");
			}
			if(!valid)
				throw exception("Invalid C-DIMSE response status");
		}

		void ValidateCdimseRequest(
			const DataSet& command,
			Command::Code expectedCommand,
			const UID* expectedClassUID = 0)
		{
			UINT16 commandField = 0;
			command(TAG_CMD_FIELD) >> commandField;
			if(commandField != expectedCommand)
				throw exception("Unexpected C-DIMSE request command field");

			if(expectedClassUID)
			{
				UID commandClassUID;
				command(TAG_AFF_SOP_CLASS_UID) >> commandClassUID;
				if(commandClassUID != *expectedClassUID)
					throw exception("Unexpected C-DIMSE request SOP Class UID");
			}
		}

		void ReadRequiredCommand(ServiceBase& service, DataSet& command)
		{
			if(!service.Read(command))
				throw exception("Unexpected association release while reading C-DIMSE response command");
		}

		void ReadRequiredDataSet(ServiceBase& service, DataSet& data)
		{
			if(!service.Read(data))
				throw exception("Unexpected association release while reading C-DIMSE response data set");
		}

		void ReadRequiredRequestDataSet(ServiceBase& service, DataSet& data)
		{
			if(!service.Read(data))
				throw exception("Unexpected association release while reading C-DIMSE request data set");
		}

		void SetCGetCounters(CommandSet::CGetRSP& response, const CSubOperationResult& result)
		{
			response.setRemaining(result.remaining);
			response.setCompleted(result.completed);
			response.setFailed(result.failed);
			response.setWarning(result.warning);
		}

		void SetCMoveCounters(CommandSet::CMoveRSP& response, const CSubOperationResult& result)
		{
			response.setRemaining(result.remaining);
			response.setCompleted(result.completed);
			response.setFailed(result.failed);
			response.setWarning(result.warning);
		}

		void RequireSCURole(ServiceBase& service, const UID& classUID)
		{
			if(service.HasNegotiatedRole(classUID) && !service.CanActAsSCU(classUID))
				throw exception("Association did not negotiate local SCU role for SOP Class");
		}

		void RequireSCPRole(ServiceBase& service, const UID& classUID)
		{
			if(service.HasNegotiatedRole(classUID) && !service.CanActAsSCP(classUID))
				throw exception("Association did not negotiate local SCP role for SOP Class");
		}
	}

	CSubOperationResult::CSubOperationResult(
		UINT16 statusCode,
		UINT16 remainingCount,
		UINT16 completedCount,
		UINT16 failedCount,
		UINT16 warningCount)
	: status(statusCode)
	, remaining(remainingCount)
	, completed(completedCount)
	, failed(failedCount)
	, warning(warningCount)
	{
	}

	/*!
		Simply write back a success response.
		See Part 8, table 9.1-5
	*/
	void HandleCEcho(ServiceBase& pdu, const DataSet& command,const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_ECHO_RQ,&classUID);
		UINT16 msgID;
		command(TAG_MSG_ID)>>msgID;
		CommandSet::CEchoRSP response(msgID,classUID);
		pdu.WriteCommand(response,classUID);
	}

	void HandleCStore(CStoreFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_STORE_RQ,&classUID);
		UINT16 msgID,data_set_status;
		UID instuid;
		command(TAG_MSG_ID)>>msgID;
		command(TAG_DATA_SET_TYPE)>>data_set_status;
		command(TAG_AFF_SOP_INST_UID)>>instuid;
		if(data_set_status==DataSetStatus::NO_DATA_SET)
			throw exception("No data set!");
		DataSet data;
		ReadRequiredRequestDataSet(pdu,data);//the TransferSyntax is determined internally by pdu. -Sam

		handler(pdu,command,data);//this should indicate failure via a throw...

		CommandSet::CStoreRSP response(msgID,classUID,instuid,Status::SUCCESS);
		pdu.WriteCommand(response,classUID);
	}

	/*
		Part 7, Section 9.1.2.2 describes this procedure...
					also table 9.3-3
		Part 4, Section C.3.4 has additional information.
	*/
	namespace
	{
		UINT16 CallLegacyCFindHandler(CFindFunction handler, ServiceBase& pdu, DataSet& requestData, Sequence& matches)
		{
			handler(pdu, requestData, matches);
			return Status::SUCCESS;
		}
	}

	void HandleCFind(CFindStatusFunction handler,ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_FIND_RQ,&classUID);
#ifdef _DEBUG
		cout  << "HandleCFind:" << endl << command;
#endif
		UINT16 msgID,data_set_status;
		command(TAG_MSG_ID)>>msgID;
		pdu.ClearCancelRequest(msgID);
		command(TAG_DATA_SET_TYPE)>>data_set_status;
		if(data_set_status==DataSetStatus::NO_DATA_SET)
			throw exception("No data set");
		DataSet request_data;
		ReadRequiredRequestDataSet(pdu,request_data);

		Sequence Matches;

		//the user-defined callback does the actual matching...
		const UINT16 finalStatus = handler(pdu,request_data,Matches);

		if(PollCCancelRQ(pdu,msgID))
		{
			CommandSet::CFindRSP response(msgID,classUID,Status::CANCEL,DataSetStatus::NO_DATA_SET);
			pdu.WriteCommand(response,classUID);
			return;
		}

		//now we send back all found matches.
		for(Sequence::iterator I=Matches.begin();I!=Matches.end();I++)
		{

			CommandSet::CFindRSP response(msgID,classUID,Status::PENDING,DataSetStatus::YES_DATA_SET);
			pdu.WriteCommand(response,classUID);
			pdu.WriteDataSet(*I,classUID);
		}

		CommandSet::CFindRSP response(msgID,classUID,finalStatus,DataSetStatus::NO_DATA_SET);
		pdu.WriteCommand(response,classUID);
	}

	void HandleCFind(CFindFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		HandleCFind(
			CFindStatusFunction(
				[&](ServiceBase& service, DataSet& requestData, Sequence& matches)
				{
					return CallLegacyCFindHandler(handler, service, requestData, matches);
				}),
			pdu,
			command,
			classUID);
	}



	/*
		C-GET is only maintained in
		the standard for backwards compatability.  If we're going to implement it, I
		think we need to figure out the client-side behaviour first.
	*/


	void CGetSCP::handle(ServiceBase& pdu, const DataSet& rqCmd, const UID& classUID)
	{
		(void)pdu;
		(void)rqCmd;
		(void)classUID;
		//TODO
		throw NotYetImplemented();
	}

	void HandleCGet(CGetFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_GET_RQ,&classUID);
		UINT16 msgID,data_set_status;
		command(TAG_MSG_ID)>>msgID;
		pdu.ClearCancelRequest(msgID);
		command(TAG_DATA_SET_TYPE)>>data_set_status;
		if(data_set_status==DataSetStatus::NO_DATA_SET)
			throw exception("No data set");
		DataSet request_data;
		ReadRequiredRequestDataSet(pdu,request_data);

		handler(pdu,command,request_data);
	}

	void HandleCGet(CGetStatusFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_GET_RQ,&classUID);
		UINT16 msgID,data_set_status;
		command(TAG_MSG_ID)>>msgID;
		pdu.ClearCancelRequest(msgID);
		command(TAG_DATA_SET_TYPE)>>data_set_status;
		if(data_set_status==DataSetStatus::NO_DATA_SET)
			throw exception("No data set");
		DataSet request_data;
		ReadRequiredRequestDataSet(pdu,request_data);

		CSubOperationResult result = handler(pdu,command,request_data);
		if(PollCCancelRQ(pdu,msgID))
			result.status = Status::CANCEL;
		CommandSet::CGetRSP response(msgID,classUID,result.status,DataSetStatus::NO_DATA_SET);
		SetCGetCounters(response,result);
		pdu.WriteCommand(response,classUID);
	}

	void HandleCCancel(ServiceBase& pdu, const DataSet& command)
	{
		ValidateCdimseRequest(command,Command::C_CANCEL_RQ);
		UINT16 messageIDBeingRespondedTo = 0;
		UINT16 dataSetType = 0;
		command(TAG_MSG_ID_RSP) >> messageIDBeingRespondedTo;
		command(TAG_DATA_SET_TYPE) >> dataSetType;
		if(messageIDBeingRespondedTo == 0)
			throw exception("C-CANCEL-RQ references invalid Message ID");
		if(dataSetType != DataSetStatus::NO_DATA_SET)
			throw exception("C-CANCEL-RQ shall not contain a data set");
		pdu.RequestCancel(messageIDBeingRespondedTo);
	}

	bool IsCdimseSuccessStatus(UINT16 status)
	{
		return status == Status::SUCCESS;
	}

	bool IsCdimsePendingStatus(UINT16 status)
	{
		return status == Status::PENDING || status == Status::PENDING1;
	}

	bool IsCdimseCancelStatus(UINT16 status)
	{
		return status == Status::CANCEL;
	}

	bool IsCdimseWarningStatus(UINT16 status)
	{
		return status == Status::WARNING;
	}

	bool IsCdimseFinalStatus(UINT16 status)
	{
		return !IsCdimsePendingStatus(status);
	}

	namespace
	{
		bool IsUnableToProcessStatus(UINT16 status)
		{
			return (status & 0xf000) == 0xc000;
		}

		bool IsA7xxStatus(UINT16 status)
		{
			return (status & 0xff00) == 0xa700;
		}

		bool IsA9xxStatus(UINT16 status)
		{
			return (status & 0xff00) == 0xa900;
		}
	}

	bool IsCEchoResponseStatus(UINT16 status)
	{
		return IsCdimseSuccessStatus(status) ||
			status == 0x0122 ||
			status == 0x0210 ||
			status == 0x0211 ||
			status == 0x0212;
	}

	bool IsCStoreResponseStatus(UINT16 status)
	{
		return IsCdimseSuccessStatus(status) ||
			status == 0xb000 ||
			status == 0xb006 ||
			status == 0xb007 ||
			IsA7xxStatus(status) ||
			IsA9xxStatus(status) ||
			IsUnableToProcessStatus(status);
	}

	bool IsCFindResponseStatus(UINT16 status)
	{
		return IsCdimseSuccessStatus(status) ||
			status == Status::CANCEL ||
			status == Status::PENDING ||
			status == Status::PENDING1 ||
			status == 0xa700 ||
			status == 0xa900 ||
			IsUnableToProcessStatus(status);
	}

	bool IsCGetResponseStatus(UINT16 status)
	{
		return IsCdimseSuccessStatus(status) ||
			status == Status::CANCEL ||
			status == Status::WARNING ||
			status == Status::PENDING ||
			status == 0xa701 ||
			status == 0xa702 ||
			status == 0xa900 ||
			IsUnableToProcessStatus(status);
	}

	bool IsCMoveResponseStatus(UINT16 status)
	{
		return IsCdimseSuccessStatus(status) ||
			status == Status::CANCEL ||
			status == Status::WARNING ||
			status == Status::PENDING ||
			status == 0xa701 ||
			status == 0xa702 ||
			status == 0xa801 ||
			status == 0xa900 ||
			IsUnableToProcessStatus(status);
	}

	bool PollCCancelRQ(ServiceBase& pdu, UINT16 messageID)
	{
		Network::Socket* socket = pdu.GetSocket();
		if(!socket || !socket->MoreData(0))
			return pdu.IsCancelRequested(messageID);

		DataSet command;
		if(!pdu.Read(command))
			return false;

		Command::Code commandField = 0;
		command(TAG_CMD_FIELD) >> commandField;
		if(commandField != Command::C_CANCEL_RQ)
			throw exception("Expected C-CANCEL-RQ while polling for cancellation");

		HandleCCancel(pdu, command);
		if(messageID == 0)
			return true;
		return pdu.IsCancelRequested(messageID);
	}

	CSubOperationResult SendCGetStoreSubOperations(ServiceBase& pdu, const Sequence& instances)
	{
		if(instances.size() > 0xffff)
			throw exception("Too many C-GET sub-operations for UINT16 counters");

		CSubOperationResult result(Status::SUCCESS, static_cast<UINT16>(instances.size()), 0, 0, 0);

		for(Sequence::const_iterator I=instances.begin();I!=instances.end();I++)
		{
			if(PollCCancelRQ(pdu))
			{
				result.status = Status::CANCEL;
				return result;
			}

			UID classUID;
			UID instUID;
			(*I)(TAG_SOP_CLASS_UID) >> classUID;
			(*I)(TAG_SOP_INST_UID) >> instUID;

			RequireSCURole(pdu,classUID);
			CStoreSCU storeSCU(pdu,classUID);
			storeSCU.writeRQ(instUID,*I);

			UINT16 storeStatus = 0;
			DataSet storeResponse;
			storeSCU.readRSP(storeStatus,storeResponse);

			result.remaining--;
			if(IsCdimseSuccessStatus(storeStatus))
				result.completed++;
			else if(IsCdimseWarningStatus(storeStatus))
				result.warning++;
			else
				result.failed++;
		}

		if(result.failed != 0 || result.warning != 0)
			result.status = Status::WARNING;
		return result;
	}

	CSubOperationResult SendCMoveStoreSubOperations(
		ServiceBase& destination,
		const Sequence& instances,
		const std::string& moveOriginatorAET,
		UINT16 moveOriginatorMessageID)
	{
		return SendCMoveStoreSubOperations(
			destination,
			instances,
			moveOriginatorAET,
			moveOriginatorMessageID,
			destination,
			0);
	}

	CSubOperationResult SendCMoveStoreSubOperations(
		ServiceBase& destination,
		const Sequence& instances,
		const std::string& moveOriginatorAET,
		UINT16 moveOriginatorMessageID,
		ServiceBase& cancelService,
		UINT16 cancelMessageID)
	{
		if(instances.size() > 0xffff)
			throw exception("Too many C-MOVE sub-operations for UINT16 counters");

		CSubOperationResult result(Status::SUCCESS, static_cast<UINT16>(instances.size()), 0, 0, 0);

		for(Sequence::const_iterator I=instances.begin();I!=instances.end();I++)
		{
			if(cancelMessageID != 0 && PollCCancelRQ(cancelService,cancelMessageID))
			{
				result.status = Status::CANCEL;
				return result;
			}

			UID classUID;
			UID instUID;
			(*I)(TAG_SOP_CLASS_UID) >> classUID;
			(*I)(TAG_SOP_INST_UID) >> instUID;

			RequireSCURole(destination,classUID);
			CStoreSCU storeSCU(destination,classUID);
			storeSCU.writeMoveRQ(instUID,*I,moveOriginatorAET,moveOriginatorMessageID);

			UINT16 storeStatus = 0;
			DataSet storeResponse;
			storeSCU.readRSP(storeStatus,storeResponse);

			result.remaining--;
			if(IsCdimseSuccessStatus(storeStatus))
				result.completed++;
			else if(IsCdimseWarningStatus(storeStatus))
				result.warning++;
			else
				result.failed++;
		}

		if(result.failed != 0 || result.warning != 0)
			result.status = Status::WARNING;
		return result;
	}

	CSubOperationResult SendCMoveStoreSubOperationsToEndpoint(
		const std::string& host,
		unsigned short port,
		const std::string& localAET,
		const std::string& remoteAET,
		const PresentationContexts& presentationContexts,
		const Sequence& instances,
		const std::string& moveOriginatorAET,
		UINT16 moveOriginatorMessageID)
	{
		ClientConnection destination(host,port,localAET,remoteAET,presentationContexts);
		return SendCMoveStoreSubOperations(
			destination,
			instances,
			moveOriginatorAET,
			moveOriginatorMessageID);
	}

	CSubOperationResult SendCMoveStoreSubOperationsToEndpoint(
		const std::string& host,
		unsigned short port,
		const std::string& localAET,
		const std::string& remoteAET,
		const PresentationContexts& presentationContexts,
		const Sequence& instances,
		const std::string& moveOriginatorAET,
		UINT16 moveOriginatorMessageID,
		ServiceBase& cancelService,
		UINT16 cancelMessageID)
	{
		ClientConnection destination(host,port,localAET,remoteAET,presentationContexts);
		return SendCMoveStoreSubOperations(
			destination,
			instances,
			moveOriginatorAET,
			moveOriginatorMessageID,
			cancelService,
			cancelMessageID);
	}

	void HandleCMove(CMoveFunction handler,ServiceBase& pdu,
		const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_MOVE_RQ,&classUID);
		UINT16 msgID,data_set_status;
		command(TAG_MSG_ID)>>msgID;
		pdu.ClearCancelRequest(msgID);
		command(TAG_DATA_SET_TYPE)>>data_set_status;
		if(data_set_status==DataSetStatus::NO_DATA_SET)
			throw exception("No data set");
		DataSet request_data;
		ReadRequiredRequestDataSet(pdu,request_data);

		//The rest part of implementation involves design of server and should be 
		//implemented in serve. -Sam
		handler(pdu,command,request_data);
	}

	void HandleCMove(CMoveStatusFunction handler,ServiceBase& pdu,
		const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateCdimseRequest(command,Command::C_MOVE_RQ,&classUID);
		UINT16 msgID,data_set_status;
		command(TAG_MSG_ID)>>msgID;
		pdu.ClearCancelRequest(msgID);
		command(TAG_DATA_SET_TYPE)>>data_set_status;
		if(data_set_status==DataSetStatus::NO_DATA_SET)
			throw exception("No data set");
		DataSet request_data;
		ReadRequiredRequestDataSet(pdu,request_data);

		CSubOperationResult result = handler(pdu,command,request_data);
		if(PollCCancelRQ(pdu,msgID))
			result.status = Status::CANCEL;
		CommandSet::CMoveRSP response(msgID,classUID,result.status,DataSetStatus::NO_DATA_SET);
		SetCMoveCounters(response,result);
		pdu.WriteCommand(response,classUID);
	}



	CEchoSCU::CEchoSCU(ServiceBase& service)
	: SCU(service,VERIFICATION_SOP_CLASS)
	{
	}

	void SCU::writeCancelForLastRQ()
	{
		if(lastMessageID_ == 0)
			throw exception("Cannot send C-CANCEL-RQ before a request has been sent");
		CommandSet::CCancelRQ rq(lastMessageID_);
		service_.WriteCommand(rq, classUID_);
	}

	void CEchoSCU::writeRQ()
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::CEchoRQ rq(lastMessageID_, classUID_);
		service_.WriteCommand(rq, classUID_) ;
	}

	void CEchoSCU::readRSP(UINT16& status)
	{
		DataSet response;
		readRSP(status, response);
	}

	void CEchoSCU::readRSP(UINT16& status, DataSet& response)
	{
		ReadRequiredCommand(service_,response);
		ValidateCdimseResponse(response, Command::C_ECHO_RSP, lastMessageID_, classUID_);
		response(TAG_STATUS)>>status;
		ValidateCdimseResponseStatus(status, Command::C_ECHO_RSP);
	}

	CStoreSCU::CStoreSCU(ServiceBase& service,const UID& classUID)
	: SCU(service,classUID)
	, lastSOPInstanceUID_("")
	{
	}

	void CStoreSCU::writeRQ(const UID& instUID, const DataSet& data,/*TS ts,*/ UINT16 priority)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		lastSOPInstanceUID_ = instUID;
		CommandSet::CStoreRQ rq(lastMessageID_, classUID_, instUID, priority);
		service_.WriteCommand(rq, classUID_);
		service_.WriteDataSet(data, classUID_/*,ts*/);
	}

	void CStoreSCU::writeMoveRQ(
		const UID& instUID,
		const DataSet& data,
		const std::string& moveOriginatorAET,
		UINT16 moveOriginatorMessageID,
		UINT16 priority)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		lastSOPInstanceUID_ = instUID;
		CommandSet::CStoreRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			moveOriginatorAET,
			moveOriginatorMessageID,
			priority);
		service_.WriteCommand(rq, classUID_);
		service_.WriteDataSet(data, classUID_);
	}

	void CStoreSCU::readRSP(UINT16& status)//maybe status should be a return value?TODO
	{
		DataSet response;
		readRSP(status, response);
	}

	void CStoreSCU::readRSP(UINT16& status, DataSet& response)
	{
		ReadRequiredCommand(service_,response);
		ValidateCdimseResponse(response, Command::C_STORE_RSP, lastMessageID_, classUID_, &lastSOPInstanceUID_);
		response(TAG_STATUS) >> status;
		ValidateCdimseResponseStatus(status, Command::C_STORE_RSP);
	}
//I'd prefer:
/*
		DataSet CStoreSCU::readRSP();
*/

	CFindSCU::CFindSCU(ServiceBase& service,const UID& classUID)
	: SCU(service,classUID)
	{
	}

	void CFindSCU::writeRQ(const DataSet& data, UINT16 priority)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::CFindRQ rq(lastMessageID_, classUID_, priority);
		service_.WriteCommand(rq, classUID_);
		service_.WriteDataSet(data, classUID_);
	}

	void CFindSCU::writeCancelRQ()
	{
		writeCancelForLastRQ();
	}

	void CFindSCU::readRSP(UINT16& status, DataSet&  data)
	{
		DataSet response;
		readRSP(status, response, data);
	}


	//All the ::readRSP functions from here on are identical: please
	//amalgamate!

	void CFindSCU::readRSP(UINT16& status, DataSet& response, DataSet&  data)
	{
		UINT16 dstype = 0;

		ReadRequiredCommand(service_,response);
		ValidateCdimseResponse(response, Command::C_FIND_RSP, lastMessageID_, classUID_);
		response(TAG_DATA_SET_TYPE)	>>	dstype;
		response(TAG_STATUS)		>>	status;
		ValidateCdimseResponseStatus(status, Command::C_FIND_RSP);
		if(dstype!=DataSetStatus::NO_DATA_SET)
			ReadRequiredDataSet(service_,data);

	}

	CGetSCU::CGetSCU(ServiceBase& service,const UID& classUID)
	: SCU(service,classUID)
	{
	}

	void CGetSCU::writeRQ(const DataSet& data, UINT16 priority)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::CGetRQ rq(lastMessageID_, classUID_, priority);
		service_.WriteCommand(rq, classUID_);
		service_.WriteDataSet(data, classUID_);
	}

	void CGetSCU::writeCancelRQ()
	{
		writeCancelForLastRQ();
	}

	void CGetSCU::readRSP(UINT16& status, DataSet&  data)
	{
		DataSet rsp;
		readRSP(status, rsp, data);
	}

	void CGetSCU::readRSP(UINT16& status, DataSet& response, DataSet&  data)
	{
		UINT16 dstype = 0;
		ReadRequiredCommand(service_,response);
		ValidateCdimseResponse(response, Command::C_GET_RSP, lastMessageID_, classUID_);
		response(TAG_DATA_SET_TYPE)	>>	dstype;
		response(TAG_STATUS)		>>	status;
		ValidateCdimseResponseStatus(status, Command::C_GET_RSP);
		if(dstype!=DataSetStatus::NO_DATA_SET)
			ReadRequiredDataSet(service_,data);

	}

	void CGetSCU::readRSP(UINT16& status, DataSet& response, DataSet& data, CStoreFunction storeHandler)
	{
		while(true)
		{
			DataSet command;
			ReadRequiredCommand(service_,command);

			Command::Code commandField = 0;
			command(TAG_CMD_FIELD) >> commandField;

			if(commandField == Command::C_GET_RSP)
			{
				UINT16 dstype = 0;
				ValidateCdimseResponse(command, Command::C_GET_RSP, lastMessageID_, classUID_);
				command(TAG_DATA_SET_TYPE) >> dstype;
				command(TAG_STATUS) >> status;
				ValidateCdimseResponseStatus(status, Command::C_GET_RSP);
				response = command;
				if(dstype!=DataSetStatus::NO_DATA_SET)
					ReadRequiredDataSet(service_,data);
				return;
			}

			if(commandField == Command::C_STORE_RQ)
			{
				UID storeClassUID("");
				command(TAG_AFF_SOP_CLASS_UID) >> storeClassUID;
				HandleCStore(storeHandler, service_, command, storeClassUID);
				continue;
			}

			throw exception("Unexpected C-DIMSE command while reading C-GET response");
		}
	}

	CMoveSCU::CMoveSCU(ServiceBase& service,const UID& classUID)
	: SCU(service,classUID)
	{
	}

	void CMoveSCU::writeRQ(const string& destAET,
							const DataSet& data, UINT16 priority)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::CMoveRQ rq(lastMessageID_, classUID_, destAET, priority);
		service_.WriteCommand(rq, classUID_);
		service_.WriteDataSet(data, classUID_);
	}

	void CMoveSCU::writeCancelRQ()
	{
		writeCancelForLastRQ();
	}

/*
	Now I'm not happy about these extra readRSP members, one of them
	is superfluous
*/

	void CMoveSCU::readRSP(UINT16& status, DataSet&  data)
	{
		DataSet response;
		readRSP(status, response, data);
	}

	void CMoveSCU::readRSP(UINT16& status, DataSet& response, DataSet&  data)
	{
		UINT16 dstype = 0;

		ReadRequiredCommand(service_,response);
		ValidateCdimseResponse(response, Command::C_MOVE_RSP, lastMessageID_, classUID_);
		response(TAG_DATA_SET_TYPE)	>>	dstype;
		response(TAG_STATUS)		>>	status;
		ValidateCdimseResponseStatus(status, Command::C_MOVE_RSP);
		if(dstype!=DataSetStatus::NO_DATA_SET)
			ReadRequiredDataSet(service_,data);

	}
}//namespace dicom
