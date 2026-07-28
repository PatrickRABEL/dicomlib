/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/
#include <string>
#include "Ndimse.hpp"
#include "CommandSets.hpp"
#include "Types.hpp"

#include "ImplementationUID.hpp"
#include "ServiceBase.hpp"
#include "Utility.hpp"

namespace dicom
{
	namespace
	{
		bool IsStatusRange(UINT16 status, UINT16 highNibble)
		{
			return (status & 0xf000) == highNibble;
		}

		void ValidateNdimseResponse(
			const DataSet& response,
			Command::Code expectedCommand,
			UINT16 expectedMessageID,
			const UID& expectedClassUID)
		{
			UINT16 command = 0;
			UINT16 responseMessageID = 0;
			UID responseClassUID;
			response(TAG_CMD_FIELD) >> command;
			response(TAG_MSG_ID_RSP) >> responseMessageID;
			response(TAG_AFF_SOP_CLASS_UID) >> responseClassUID;

			if(command != expectedCommand)
				throw exception("Unexpected N-DIMSE response command field");
			if(responseMessageID != expectedMessageID)
				throw exception("Unexpected N-DIMSE response message ID");
			if(responseClassUID != expectedClassUID)
				throw exception("Unexpected N-DIMSE response SOP Class UID");
		}

		void ValidateNdimseRequest(
			const DataSet& command,
			Command::Code expectedCommand,
			const UID& expectedClassUID)
		{
			UINT16 commandField = 0;
			UID commandClassUID;
			command(TAG_CMD_FIELD) >> commandField;
			if(commandField != expectedCommand)
				throw exception("Unexpected N-DIMSE request command field");

			switch(expectedCommand)
			{
			case Command::N_EVENT_REPORT_RQ:
			case Command::N_CREATE_RQ:
				command(TAG_AFF_SOP_CLASS_UID) >> commandClassUID;
				break;
			case Command::N_GET_RQ:
			case Command::N_SET_RQ:
			case Command::N_ACTION_RQ:
			case Command::N_DELETE_RQ:
				command(TAG_REQ_SOP_CLASS_UID) >> commandClassUID;
				break;
			default:
				throw exception("Unexpected N-DIMSE request command field");
			}

			if(commandClassUID != expectedClassUID)
				throw exception("Unexpected N-DIMSE request SOP Class UID");
		}

		void RequireSCURole(ServiceBase& service, const UID& classUID)
		{
			if(service.HasNegotiatedRole(classUID) && !service.CanActAsSCU(classUID))
				throw exception("Association role selection does not permit local SCU role");
		}

		void RequireSCPRole(ServiceBase& service, const UID& classUID)
		{
			if(service.HasNegotiatedRole(classUID) && !service.CanActAsSCP(classUID))
				throw exception("Association role selection does not permit local SCP role");
		}

		void ValidateNdimseResponseStatus(UINT16 status, Command::Code command)
		{
			bool valid = false;
			switch(command)
			{
			case Command::N_EVENT_REPORT_RSP:
				valid = IsNEventReportResponseStatus(status);
				break;
			case Command::N_GET_RSP:
				valid = IsNGetResponseStatus(status);
				break;
			case Command::N_SET_RSP:
				valid = IsNSetResponseStatus(status);
				break;
			case Command::N_ACTION_RSP:
				valid = IsNActionResponseStatus(status);
				break;
			case Command::N_CREATE_RSP:
				valid = IsNCreateResponseStatus(status);
				break;
			case Command::N_DELETE_RSP:
				valid = IsNDeleteResponseStatus(status);
				break;
			default:
				throw exception("Unexpected N-DIMSE response command field");
			}
			if(!valid)
				throw exception("Invalid N-DIMSE response status");
		}

		void ReadRequestDataSetIfPresent(ServiceBase& service, const DataSet& command, DataSet& data)
		{
			UINT16 dataSetType = 0;
			command(TAG_DATA_SET_TYPE) >> dataSetType;
			if(dataSetType != DataSetStatus::NO_DATA_SET && !service.Read(data))
				throw exception("Unexpected association release while reading N-DIMSE request data set");
		}

		void ReadRequiredRequestDataSet(ServiceBase& service, const DataSet& command, DataSet& data)
		{
			UINT16 dataSetType = 0;
			command(TAG_DATA_SET_TYPE) >> dataSetType;
			if(dataSetType == DataSetStatus::NO_DATA_SET)
				throw exception("N-DIMSE request requires a data set");
			if(!service.Read(data))
				throw exception("Unexpected association release while reading N-DIMSE request data set");
		}

		void RejectRequestDataSet(const DataSet& command)
		{
			UINT16 dataSetType = 0;
			command(TAG_DATA_SET_TYPE) >> dataSetType;
			if(dataSetType != DataSetStatus::NO_DATA_SET)
				throw exception("N-DIMSE request shall not include a data set");
		}

		UID ReadUIDIfPresent(const DataSet& command, Tag tag)
		{
			UID uid("");
			if(command.exists(tag))
				command(tag) >> uid;
			return uid;
		}
	}

	bool IsNdimseSuccessStatus(UINT16 status)
	{
		return status == Status::SUCCESS;
	}

	bool IsNdimseWarningStatus(UINT16 status)
	{
		return status == 0x0001 ||
			status == 0x0107 ||
			status == 0x0116 ||
			IsStatusRange(status,0xb000);
	}

	bool IsNdimseFailureStatus(UINT16 status)
	{
		if(IsStatusRange(status,0xa000) || IsStatusRange(status,0xc000))
			return true;
		if((status & 0xff00) == 0x0200)
			return true;
		if((status & 0xff00) == 0x0100)
			return status != 0x0107 && status != 0x0116;
		return false;
	}

	bool IsNdimseFinalStatus(UINT16 status)
	{
		return IsNdimseSuccessStatus(status) ||
			IsNdimseWarningStatus(status) ||
			IsNdimseFailureStatus(status);
	}

	bool IsNEventReportResponseStatus(UINT16 status)
	{
		return IsNdimseFinalStatus(status);
	}

	bool IsNGetResponseStatus(UINT16 status)
	{
		return IsNdimseFinalStatus(status);
	}

	bool IsNSetResponseStatus(UINT16 status)
	{
		return IsNdimseFinalStatus(status);
	}

	bool IsNActionResponseStatus(UINT16 status)
	{
		return IsNdimseFinalStatus(status);
	}

	bool IsNCreateResponseStatus(UINT16 status)
	{
		return IsNdimseFinalStatus(status);
	}

	bool IsNDeleteResponseStatus(UINT16 status)
	{
		return IsNdimseFinalStatus(status);
	}

	void HandleNEventReport(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_EVENT_REPORT_RQ,classUID);

		UINT16 msgID = 0;
		UINT16 eventTypeID = 0;
		command(TAG_MSG_ID) >> msgID;
		command(TAG_EVENT_TYPE_ID) >> eventTypeID;
		UID instUID = ReadUIDIfPresent(command,TAG_AFF_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		ReadRequestDataSetIfPresent(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNEventReportResponseStatus(status))
			throw exception("Invalid N-EVENT-REPORT response status");

		CommandSet::NEventReportRSP responseCommand(
			msgID,
			classUID,
			instUID,
			status,
			eventTypeID,
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(responseCommand,classUID);
		if(!responseData.empty())
			pdu.WriteDataSet(responseData,classUID);
	}

	void HandleNGet(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_GET_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		RejectRequestDataSet(command);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNGetResponseStatus(status))
			throw exception("Invalid N-GET response status");

		CommandSet::NGetRSP responseCommand(
			msgID,
			classUID,
			instUID,
			status,
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(responseCommand,classUID);
		if(!responseData.empty())
			pdu.WriteDataSet(responseData,classUID);
	}

	void HandleNSet(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_SET_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		ReadRequiredRequestDataSet(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNSetResponseStatus(status))
			throw exception("Invalid N-SET response status");

		CommandSet::NSetRSP responseCommand(
			msgID,
			classUID,
			instUID,
			status,
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(responseCommand,classUID);
		if(!responseData.empty())
			pdu.WriteDataSet(responseData,classUID);
	}

	void HandleNAction(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_ACTION_RQ,classUID);

		UINT16 msgID = 0;
		UINT16 actionTypeID = 0;
		command(TAG_MSG_ID) >> msgID;
		command(TAG_ACTION_TYPE_ID) >> actionTypeID;
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		ReadRequestDataSetIfPresent(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNActionResponseStatus(status))
			throw exception("Invalid N-ACTION response status");

		CommandSet::NActionRSP responseCommand(
			msgID,
			classUID,
			instUID,
			status,
			actionTypeID,
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(responseCommand,classUID);
		if(!responseData.empty())
			pdu.WriteDataSet(responseData,classUID);
	}

	void HandleNCreate(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_CREATE_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		UID instUID = ReadUIDIfPresent(command,TAG_AFF_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		ReadRequestDataSetIfPresent(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNCreateResponseStatus(status))
			throw exception("Invalid N-CREATE response status");

		CommandSet::NCreateRSP responseCommand(
			msgID,
			classUID,
			instUID,
			status,
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(responseCommand,classUID);
		if(!responseData.empty())
			pdu.WriteDataSet(responseData,classUID);
	}

	void HandleNDelete(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_DELETE_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		RejectRequestDataSet(command);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNDeleteResponseStatus(status))
			throw exception("Invalid N-DELETE response status");
		if(!responseData.empty())
			throw exception("N-DELETE response shall not include a data set");

		CommandSet::NDeleteRSP responseCommand(msgID,classUID,instUID,status);
		pdu.WriteCommand(responseCommand,classUID);
	}

	NSCU::NSCU(ServiceBase& service, const UID& classUID)
		:service_(service)
		,classUID_(classUID)
		,lastMessageID_(0)
	{
	}

	void NSCU::readRSP(
		UINT16& status,
		DataSet& response,
		DataSet& data,
		Command::Code expectedCommand)
	{
		UINT16 dataSetType = 0;
		if(!service_.Read(response))
			throw exception("Unexpected association release while reading N-DIMSE response command");
		ValidateNdimseResponse(response,expectedCommand,lastMessageID_,classUID_);
		response(TAG_DATA_SET_TYPE) >> dataSetType;
		response(TAG_STATUS) >> status;
		ValidateNdimseResponseStatus(status,expectedCommand);
		if(dataSetType!=DataSetStatus::NO_DATA_SET && !service_.Read(data))
			throw exception("Unexpected association release while reading N-DIMSE response data set");
	}

	NEventReportSCU::NEventReportSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NEventReportSCU::writeRQ(const UID& instUID, UINT16 eventTypeID, const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NEventReportRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			eventTypeID,
			DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,classUID_);
		service_.WriteDataSet(data,classUID_);
	}

	void NEventReportSCU::writeRQ(const UID& instUID, UINT16 eventTypeID)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NEventReportRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			eventTypeID,
			DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,classUID_);
	}

	void NEventReportSCU::readRSP(UINT16& status, DataSet& response, DataSet& data)
	{
		NSCU::readRSP(status,response,data,Command::N_EVENT_REPORT_RSP);
	}

	NGetSCU::NGetSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NGetSCU::writeRQ(const UID& instUID, const std::vector<Tag>& attrList)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NGetRQ rq(lastMessageID_,classUID_,instUID,attrList);
		service_.WriteCommand(rq,classUID_);
	}

	void NGetSCU::readRSP(UINT16& status, DataSet& response, DataSet& data)
	{
		NSCU::readRSP(status,response,data,Command::N_GET_RSP);
	}

	NSetSCU::NSetSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NSetSCU::writeRQ(const UID& instUID, const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NSetRQ rq(lastMessageID_,classUID_,instUID);
		service_.WriteCommand(rq,classUID_);
		service_.WriteDataSet(data,classUID_);
	}

	void NSetSCU::readRSP(UINT16& status, DataSet& response, DataSet& data)
	{
		NSCU::readRSP(status,response,data,Command::N_SET_RSP);
	}

	NActionSCU::NActionSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NActionSCU::writeRQ(const UID& instUID, UINT16 actionTypeID, const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NActionRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			actionTypeID,
			DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,classUID_);
		service_.WriteDataSet(data,classUID_);
	}

	void NActionSCU::writeRQ(const UID& instUID, UINT16 actionTypeID)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NActionRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			actionTypeID,
			DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,classUID_);
	}

	void NActionSCU::readRSP(UINT16& status, DataSet& response, DataSet& data)
	{
		NSCU::readRSP(status,response,data,Command::N_ACTION_RSP);
	}

	NCreateSCU::NCreateSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NCreateSCU::writeRQ(const UID& instUID, const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,instUID,DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,classUID_);
		service_.WriteDataSet(data,classUID_);
	}

	void NCreateSCU::writeRQ(const UID& instUID)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,instUID,DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,classUID_);
	}

	void NCreateSCU::writeRQ(const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,classUID_);
		service_.WriteDataSet(data,classUID_);
	}

	void NCreateSCU::writeRQ()
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,classUID_);
	}

	void NCreateSCU::readRSP(UINT16& status, DataSet& response, DataSet& data)
	{
		NSCU::readRSP(status,response,data,Command::N_CREATE_RSP);
	}

	NDeleteSCU::NDeleteSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NDeleteSCU::writeRQ(const UID& instUID)
	{
		RequireSCURole(service_,classUID_);
		lastMessageID_ = uniq16odd();
		CommandSet::NDeleteRQ rq(lastMessageID_,classUID_,instUID);
		service_.WriteCommand(rq,classUID_);
	}

	void NDeleteSCU::readRSP(UINT16& status, DataSet& response, DataSet& data)
	{
		NSCU::readRSP(status,response,data,Command::N_DELETE_RSP);
	}
}


#ifdef THIS_ISNT_IMPLEMENTED_YET



/*
	we really need to write a test application
	that makes use of this code.

	Until then there's not much point in mucking around with
	this as we have no way of testing it.
*/
namespace dicom
{


	void HandleNCreate(NHandlerFunction handler,ServiceBase& pdu, const DataSet& command, const UID& classUID)
//	bool NCreateSCP::handle(ServiceBase& pdu, const DataSet& rqCmd, const UID& classUID)
	{
		UINT16 msgID = 0;
		command(TAG_MSG_ID)>>msgID;

		UINT16 dstype = 0;
		command(TAG_DATA_SET_TYPE)>>dstype;


		UID instUID (command.Get<UID>(TAG_AFF_SOP_INST_UID));

		if(instUID.str().size()==0)//this should never happen 
		{
			char buffer[64];
			instUID = makeUID(buffer);
		}

		DataSet rqData;
		if (dstype != DataSetStatus::NO_DATA_SET)
		{
			if (pdu.Read(rqData) == false)
				return false;
		}

		UINT16 stat = Status::SUCCESS;
		DataSet rspData;

		//if (m_pHandler)
		//	stat = m_pHandler->handle(pdu/*.GetSocketfd()*/, &rspData, instUID, command, rqData);

		handler(pdu,rspData,command,data);


		NCreateRSP rspCmd(msgID, classUID, instUID, stat,
			rspData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);

		pdu.WriteCommand(rspCmd, classUID);

		if(!rspData.empty())
			pdu.WriteDataSet(rspData, classUID) ;
		
		//return true;
	}

//	bool NSetSCP::handle(ServiceBase& pdu, const DataSet& rqCmd, const UID& classUID)
	void HandleNSetSCP(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);
	{
		UINT16 msgID = 0;

		command(TAG_MSG_ID)>>msgID;

		UINT16 dstype = 0;

		command(TAG_DATA_SET_TYPE)>>dstype;

		UID instUID = command.Get<UID>(TAG_REQ_SOP_INST_UID);

		if (dstype == DataSetStatus::NO_DATA_SET)
			throw exception("No data set!");

		DataSet rqData;
		if (pdu.Read(rqData) == false)
			return false;//should throw 

		UINT16 stat = Status::SUCCESS;

		DataSet rspData;
		//if (m_pHandler)
		//	stat = m_pHandler->handle(pdu/*.GetSocketfd()*/, &rspData, instUID, command, rqData);
		handler(pdu,command,rqData);

		NSetRSP rspCmd(msgID, classUID, stat,
			rspData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET);

		pdu.WriteCommand(rspCmd, classUID); 
		if(!rspData.empty() )
			pdu.WriteDataSet(rspData, classUID);
		return true;
	}

	NCreateSCU::NCreateSCU(const UID& classUID)
		: m_classUID(classUID)
	{
	}

	void NCreateSCU::writeRQ(ServiceBase& pdu, const UID& instUID, const DataSet& data)
	{
		NCreateRQ rqCmd(uniq16odd(), m_classUID, instUID, DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(rqCmd, m_classUID);
		pdu.WriteDataSet(data, m_classUID);
	}

	void NCreateSCU::writeRQ(ServiceBase& pdu, const DataSet& data)
	{
		NCreateRQ rqCmd(uniq16odd(), m_classUID, DataSetStatus::YES_DATA_SET);
		pdu.WriteCommand(rqCmd, m_classUID);
		pdu.WriteDataSet(data, m_classUID);
	}

	void NCreateSCU::writeRQ(ServiceBase& pdu, const UID& instUID)
	{
		NCreateRQ rqCmd(uniq16odd(), m_classUID, instUID, DataSetStatus::NO_DATA_SET);
		pdu.WriteCommand(rqCmd, m_classUID);
	}

	void NCreateSCU::writeRQ(ServiceBase& pdu)
	{
		NCreateRQ rqCmd(uniq16odd(), m_classUID, DataSetStatus::NO_DATA_SET);
		pdu.WriteCommand(rqCmd, m_classUID);
	}

	bool NCreateSCU::readRSP(UINT16& status, DataSet& data, ServiceBase& pdu)
	{
		DataSet response;
		return readRSP(status, response, data, pdu);
	}

	bool NCreateSCU::readRSP(UINT16& status, DataSet& response, DataSet& data, ServiceBase& pdu)
	{
		UINT16 dstype = 0;
		pdu.Read(response);
		response(TAG_DATA_SET_TYPE)>>dstype;
		response(TAG_STATUS)>>status;
		if(dstype==DataSetStatus::NO_DATA_SET)
			return true;
		else
			return pdu.Read(data);


		//return pdu.Read(*rsp_p) != false &&
		//	rsp_p->getSafeUS(&dstype, TAG_DATA_SET_TYPE) &&
		//	rsp_p->getSafeUS(stat_p, TAG_STATUS) &&
		//	(dstype != DataSetStatus::NO_DATA_SET ? pdu.Read(*data_p) != false : true);
	}

	NSetSCU::NSetSCU(const UID& classUID)
		: m_classUID(classUID)
	{
	}

	void NSetSCU::writeRQ(ServiceBase& pdu, const UID& instUID, const DataSet& data)
	{
		NSetRQ rqCmd(uniq16odd(), m_classUID, instUID);
		pdu.WriteCommand(rqCmd, m_classUID);
		pdu.WriteDataSet(data, m_classUID);
	}

	bool NSetSCU::readRSP(UINT16& status, DataSet& data, ServiceBase& pdu)
	{
		DataSet response;
		return readRSP(status, response, data, pdu);
	}

	bool NSetSCU::readRSP(UINT16& status, DataSet& response, DataSet& data, ServiceBase& pdu)
	{
		UINT16 dstype = 0;

		pdu.Read(response);
		response(TAG_DATA_SET_TYPE)>>dstype;
		response(TAG_STATUS)>>status;
		if(dstype==DataSetStatus::NO_DATA_SET)
			return true;
		else
			return pdu.Read(data);
		//return pdu.Read(*rsp_p) != false &&
		//	rsp_p->getSafeUS(&dstype, TAG_DATA_SET_TYPE) &&
		//	rsp_p->getSafeUS(stat_p, TAG_STATUS) &&
		//	(dstype != DataSetStatus::NO_DATA_SET ? pdu.Read(*data_p) != false : true);
	}
}//namespace dicom
#endif //THIS_ISNT_IMPLEMENTED_YET
