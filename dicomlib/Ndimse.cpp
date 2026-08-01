/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/
#include <string>
#include <vector>
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
		bool IsCommandElement(Tag tag)
		{
			return (static_cast<unsigned int>(tag) & 0xffff0000u) == 0;
		}

		void ValidateCommandSetElements(const DataSet& command)
		{
			for(DataSet::const_iterator I=command.begin(); I!=command.end(); ++I)
				if(!IsCommandElement(I->first))
					throw exception("Command Set contains non-command element");
		}

		bool HasSingleNonEmptyValue(const DataSet& command, Tag tag)
		{
			const std::vector<Value> values = command.Values(tag);
			return values.size() == 1 && !values[0].empty();
		}

		bool IsStatusRange(UINT16 status, UINT16 highNibble)
		{
			return (status & 0xf000) == highNibble;
		}

		void ValidateNdimseResponse(
			const DataSet& response,
			Command::Code expectedCommand,
			UINT16 expectedMessageID,
			const UID& expectedClassUID,
			const UID* expectedInstanceUID = 0)
		{
			ValidateCommandSetElements(response);
			if(!HasSingleNonEmptyValue(response,TAG_CMD_FIELD))
				throw exception("Invalid N-DIMSE response command field");
			if(!HasSingleNonEmptyValue(response,TAG_MSG_ID_RSP))
				throw exception("Invalid N-DIMSE response message ID");
			if(response.exists(TAG_MSG_ID))
				throw exception("Unexpected N-DIMSE response message ID");
			if(response.exists(TAG_AFF_SOP_CLASS_UID) &&
				!HasSingleNonEmptyValue(response,TAG_AFF_SOP_CLASS_UID))
				throw exception("Invalid N-DIMSE response SOP Class UID");
			if(!HasSingleNonEmptyValue(response,TAG_DATA_SET_TYPE))
				throw exception("Invalid N-DIMSE response Data Set Type");
			if(!HasSingleNonEmptyValue(response,TAG_STATUS))
				throw exception("Invalid N-DIMSE response status");
			if(response.exists(TAG_AFF_SOP_INST_UID) &&
				!HasSingleNonEmptyValue(response,TAG_AFF_SOP_INST_UID))
				throw exception("Invalid N-DIMSE response SOP Instance UID");
			if(response.exists(TAG_REQ_SOP_CLASS_UID))
				throw exception("Unexpected N-DIMSE response SOP Class UID");
			if(response.exists(TAG_REQ_SOP_INST_UID))
				throw exception("Unexpected N-DIMSE response SOP Instance UID");
			if(response.exists(TAG_EVENT_TYPE_ID) &&
				!HasSingleNonEmptyValue(response,TAG_EVENT_TYPE_ID))
				throw exception("Invalid N-DIMSE response Event Type ID");
			if(response.exists(TAG_ACTION_TYPE_ID) &&
				!HasSingleNonEmptyValue(response,TAG_ACTION_TYPE_ID))
				throw exception("Invalid N-DIMSE response Action Type ID");
			if(expectedCommand != Command::N_EVENT_REPORT_RSP &&
				response.exists(TAG_EVENT_TYPE_ID))
				throw exception("Unexpected N-DIMSE response Event Type ID");
			if(expectedCommand != Command::N_ACTION_RSP &&
				response.exists(TAG_ACTION_TYPE_ID))
				throw exception("Unexpected N-DIMSE response Action Type ID");
			if(response.Values(TAG_ERR_COMMENT).size() > 1)
				throw exception("Invalid N-DIMSE response Error Comment");
			if(response.Values(TAG_ERR_ID).size() > 1)
				throw exception("Invalid N-DIMSE response Error ID");

			UINT16 command = 0;
			UINT16 responseMessageID = 0;
			response(TAG_CMD_FIELD) >> command;
			response(TAG_MSG_ID_RSP) >> responseMessageID;

			if(command != expectedCommand)
				throw exception("Unexpected N-DIMSE response command field");
			if(responseMessageID != expectedMessageID)
				throw exception("Unexpected N-DIMSE response message ID");
			if(response.exists(TAG_AFF_SOP_CLASS_UID))
			{
				UID responseClassUID;
				response(TAG_AFF_SOP_CLASS_UID) >> responseClassUID;
				if(responseClassUID != expectedClassUID)
					throw exception("Unexpected N-DIMSE response SOP Class UID");
			}
			if(expectedInstanceUID && response.exists(TAG_AFF_SOP_INST_UID))
			{
				UID responseInstanceUID;
				response(TAG_AFF_SOP_INST_UID) >> responseInstanceUID;
				if(responseInstanceUID != *expectedInstanceUID)
					throw exception("Unexpected N-DIMSE response SOP Instance UID");
			}
		}

		void ValidateNdimseRequest(
			const DataSet& command,
			Command::Code expectedCommand,
			const UID& expectedClassUID)
		{
			ValidateCommandSetElements(command);
			if(!HasSingleNonEmptyValue(command,TAG_CMD_FIELD))
				throw exception("Invalid N-DIMSE request command field");
			if(!HasSingleNonEmptyValue(command,TAG_MSG_ID))
				throw exception("Invalid N-DIMSE request message ID");
			if(command.exists(TAG_MSG_ID_RSP))
				throw exception("Unexpected N-DIMSE request message ID");
			if(!HasSingleNonEmptyValue(command,TAG_DATA_SET_TYPE))
				throw exception("Invalid N-DIMSE request Data Set Type");

			UINT16 commandField = 0;
			UID commandClassUID;
			Tag instanceTag;
			bool requiresInstanceUID = false;
			command(TAG_CMD_FIELD) >> commandField;
			if(commandField != expectedCommand)
				throw exception("Unexpected N-DIMSE request command field");

			switch(expectedCommand)
			{
			case Command::N_EVENT_REPORT_RQ:
				if(!HasSingleNonEmptyValue(command,TAG_AFF_SOP_CLASS_UID))
					throw exception("Invalid N-DIMSE request SOP Class UID");
				if(!HasSingleNonEmptyValue(command,TAG_AFF_SOP_INST_UID))
					throw exception("Invalid N-DIMSE request SOP Instance UID");
				if(command.exists(TAG_REQ_SOP_CLASS_UID))
					throw exception("Unexpected N-DIMSE request SOP Class UID");
				if(command.exists(TAG_REQ_SOP_INST_UID))
					throw exception("Unexpected N-DIMSE request SOP Instance UID");
				requiresInstanceUID = true;
				instanceTag = TAG_AFF_SOP_INST_UID;
				command(TAG_AFF_SOP_CLASS_UID) >> commandClassUID;
				break;
			case Command::N_CREATE_RQ:
				if(!HasSingleNonEmptyValue(command,TAG_AFF_SOP_CLASS_UID))
					throw exception("Invalid N-DIMSE request SOP Class UID");
				if(command.exists(TAG_AFF_SOP_INST_UID) &&
					!HasSingleNonEmptyValue(command,TAG_AFF_SOP_INST_UID))
					throw exception("Invalid N-DIMSE request SOP Instance UID");
				if(command.exists(TAG_REQ_SOP_CLASS_UID))
					throw exception("Unexpected N-DIMSE request SOP Class UID");
				if(command.exists(TAG_REQ_SOP_INST_UID))
					throw exception("Unexpected N-DIMSE request SOP Instance UID");
				command(TAG_AFF_SOP_CLASS_UID) >> commandClassUID;
				break;
			case Command::N_GET_RQ:
			case Command::N_SET_RQ:
			case Command::N_ACTION_RQ:
			case Command::N_DELETE_RQ:
				if(!HasSingleNonEmptyValue(command,TAG_REQ_SOP_CLASS_UID))
					throw exception("Invalid N-DIMSE request SOP Class UID");
				if(!HasSingleNonEmptyValue(command,TAG_REQ_SOP_INST_UID))
					throw exception("Invalid N-DIMSE request SOP Instance UID");
				if(command.exists(TAG_AFF_SOP_CLASS_UID))
					throw exception("Unexpected N-DIMSE request SOP Class UID");
				if(command.exists(TAG_AFF_SOP_INST_UID))
					throw exception("Unexpected N-DIMSE request SOP Instance UID");
				requiresInstanceUID = true;
				instanceTag = TAG_REQ_SOP_INST_UID;
				command(TAG_REQ_SOP_CLASS_UID) >> commandClassUID;
				break;
			default:
				throw exception("Unexpected N-DIMSE request command field");
			}

			if(commandClassUID != expectedClassUID)
				throw exception("Unexpected N-DIMSE request SOP Class UID");
			if(expectedCommand == Command::N_EVENT_REPORT_RQ &&
				!HasSingleNonEmptyValue(command,TAG_EVENT_TYPE_ID))
				throw exception("N-EVENT-REPORT request requires Event Type ID");
			if(expectedCommand != Command::N_EVENT_REPORT_RQ &&
				command.exists(TAG_EVENT_TYPE_ID))
				throw exception("Unexpected N-DIMSE request Event Type ID");
			if(expectedCommand == Command::N_ACTION_RQ &&
				!HasSingleNonEmptyValue(command,TAG_ACTION_TYPE_ID))
				throw exception("N-ACTION request requires Action Type ID");
			if(expectedCommand != Command::N_ACTION_RQ &&
				command.exists(TAG_ACTION_TYPE_ID))
				throw exception("Unexpected N-DIMSE request Action Type ID");
			if(requiresInstanceUID)
			{
				if(!command.exists(instanceTag))
					throw exception("N-DIMSE request requires SOP Instance UID");
				UID instanceUID;
				command(instanceTag) >> instanceUID;
				if(instanceUID.str().size() == 0)
					throw exception("N-DIMSE request requires SOP Instance UID");
			}
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

		struct PerformedOperationGuard
		{
			ServiceBase& service_;
			UINT16 messageID_;

			PerformedOperationGuard(ServiceBase& service, UINT16 messageID)
			: service_(service)
			, messageID_(messageID)
			{
				service_.BeginPerformedOperation(messageID_);
			}

			~PerformedOperationGuard()
			{
				service_.CompletePerformedOperation(messageID_);
			}
		};

		struct InvokedOperationRollback
		{
			ServiceBase& service_;
			UINT16 messageID_;
			bool active_;

			InvokedOperationRollback(ServiceBase& service, UINT16 messageID)
			: service_(service)
			, messageID_(messageID)
			, active_(true)
			{
				service_.BeginInvokedOperation(messageID_);
			}

			~InvokedOperationRollback()
			{
				if(active_)
					service_.CompleteInvokedOperation(messageID_);
			}

			void release()
			{
				active_ = false;
			}
		};

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

		bool IsNCreateDuplicateSOPInstanceStatus(UINT16 status)
		{
			return status == 0x0111;
		}

		bool IsInvalidObjectInstanceStatus(UINT16 status)
		{
			return status == 0x0117;
		}

		bool IsInvalidArgumentValueStatus(UINT16 status)
		{
			return status == 0x0115;
		}

		void ReadRequestDataSetIfPresent(ServiceBase& service, const DataSet& command, DataSet& data)
		{
			UINT16 dataSetType = 0;
			command(TAG_DATA_SET_TYPE) >> dataSetType;
			data = DataSet();
			if(dataSetType != DataSetStatus::NO_DATA_SET && !service.Read(data))
				throw exception("Unexpected association release while reading N-DIMSE request data set");
		}

		void ReadRequiredRequestDataSet(ServiceBase& service, const DataSet& command, DataSet& data)
		{
			UINT16 dataSetType = 0;
			command(TAG_DATA_SET_TYPE) >> dataSetType;
			if(dataSetType == DataSetStatus::NO_DATA_SET)
				throw exception("N-DIMSE request requires a data set");
			data = DataSet();
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
		PerformedOperationGuard performed(pdu,msgID);
		command(TAG_EVENT_TYPE_ID) >> eventTypeID;
		UID instUID = ReadUIDIfPresent(command,TAG_AFF_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		ReadRequestDataSetIfPresent(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNEventReportResponseStatus(status))
			throw exception("Invalid N-EVENT-REPORT response status");
		if(!IsNdimseSuccessStatus(status) &&
			!IsInvalidArgumentValueStatus(status) &&
			!responseData.empty())
			throw exception("N-EVENT-REPORT non-success response shall not include a data set");

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
		HandleNGet(
			[handler](ServiceBase& service, const DataSet& commandSet, const DataSet& requestData,
				DataSet& responseData, std::vector<Tag>&)
			{
				return handler(service,commandSet,requestData,responseData);
			},
			pdu,
			command,
			classUID);
	}

	void HandleNGet(NAttributeHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_GET_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		PerformedOperationGuard performed(pdu,msgID);
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		std::vector<Tag> responseAttributeList;
		RejectRequestDataSet(command);
		const UINT16 status = handler(pdu,command,requestData,responseData,responseAttributeList);
		if(!IsNGetResponseStatus(status))
			throw exception("Invalid N-GET response status");
		if(IsNdimseSuccessStatus(status) && responseData.empty())
			throw exception("N-GET success response requires an Attribute List data set");

		const UINT16 responseDataSetType =
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET;
		DataSet responseCommand =
			responseAttributeList.empty() ?
			DataSet(CommandSet::NGetRSP(msgID,classUID,instUID,status,responseDataSetType)) :
			DataSet(CommandSet::NGetRSP(msgID,classUID,instUID,status,responseDataSetType,responseAttributeList));
		pdu.WriteCommand(responseCommand,classUID);
		if(!responseData.empty())
			pdu.WriteDataSet(responseData,classUID);
	}

	void HandleNSet(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		HandleNSet(
			[handler](ServiceBase& service, const DataSet& commandSet, const DataSet& requestData,
				DataSet& responseData, std::vector<Tag>&)
			{
				return handler(service,commandSet,requestData,responseData);
			},
			pdu,
			command,
			classUID);
	}

	void HandleNSet(NAttributeHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_SET_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		PerformedOperationGuard performed(pdu,msgID);
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		std::vector<Tag> responseAttributeList;
		ReadRequiredRequestDataSet(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData,responseAttributeList);
		if(!IsNSetResponseStatus(status))
			throw exception("Invalid N-SET response status");

		const UINT16 responseDataSetType =
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET;
		DataSet responseCommand =
			responseAttributeList.empty() ?
			DataSet(CommandSet::NSetRSP(msgID,classUID,instUID,status,responseDataSetType)) :
			DataSet(CommandSet::NSetRSP(msgID,classUID,instUID,status,responseDataSetType,responseAttributeList));
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
		PerformedOperationGuard performed(pdu,msgID);
		command(TAG_ACTION_TYPE_ID) >> actionTypeID;
		UID instUID = ReadUIDIfPresent(command,TAG_REQ_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		ReadRequestDataSetIfPresent(pdu,command,requestData);
		const UINT16 status = handler(pdu,command,requestData,responseData);
		if(!IsNActionResponseStatus(status))
			throw exception("Invalid N-ACTION response status");
		if(!IsNdimseSuccessStatus(status) &&
			!IsInvalidArgumentValueStatus(status) &&
			!responseData.empty())
			throw exception("N-ACTION non-success response shall not include a data set");

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
		HandleNCreate(
			[handler](ServiceBase& service, const DataSet& commandSet, const DataSet& requestData,
				UID&, DataSet& responseData)
			{
				return handler(service,commandSet,requestData,responseData);
			},
			pdu,
			command,
			classUID);
	}

	void HandleNCreate(NCreateHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		HandleNCreate(
			[handler](ServiceBase& service, const DataSet& commandSet, const DataSet& requestData,
				UID& responseInstUID, DataSet& responseData, std::vector<Tag>&)
			{
				return handler(service,commandSet,requestData,responseInstUID,responseData);
			},
			pdu,
			command,
			classUID);
	}

	void HandleNCreate(NCreateAttributeHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID)
	{
		RequireSCPRole(pdu,classUID);
		ValidateNdimseRequest(command,Command::N_CREATE_RQ,classUID);

		UINT16 msgID = 0;
		command(TAG_MSG_ID) >> msgID;
		PerformedOperationGuard performed(pdu,msgID);
		UID instUID = ReadUIDIfPresent(command,TAG_AFF_SOP_INST_UID);

		DataSet requestData;
		DataSet responseData;
		std::vector<Tag> responseAttributeList;
		ReadRequestDataSetIfPresent(pdu,command,requestData);
		UID responseInstUID = instUID;
		const UINT16 status = handler(pdu,command,requestData,responseInstUID,responseData,responseAttributeList);
		if(!IsNCreateResponseStatus(status))
			throw exception("Invalid N-CREATE response status");
		if(IsNdimseSuccessStatus(status) && responseInstUID.str().size() == 0)
			throw exception("N-CREATE success response requires SOP Instance UID");

		const UINT16 responseDataSetType =
			responseData.empty() ? DataSetStatus::NO_DATA_SET : DataSetStatus::YES_DATA_SET;
		DataSet responseCommand =
			responseAttributeList.empty() ?
			DataSet(CommandSet::NCreateRSP(msgID,classUID,responseInstUID,status,responseDataSetType)) :
			DataSet(CommandSet::NCreateRSP(msgID,classUID,responseInstUID,status,responseDataSetType,responseAttributeList));
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
		PerformedOperationGuard performed(pdu,msgID);
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
		,contextUID_(classUID)
		,lastMessageID_(0)
		,lastSOPInstanceUID_("")
		,hasLastSOPInstanceUID_(false)
		,lastEventTypeID_(0)
		,hasLastEventTypeID_(false)
		,lastActionTypeID_(0)
		,hasLastActionTypeID_(false)
	{
	}

	void NSCU::ensureNoOutstandingRequest() const
	{
		if(lastMessageID_ != 0 && service_.IsInvokedOperationOutstanding(lastMessageID_))
			throw exception("N-DIMSE SCU object already has an outstanding request");
	}

	void NSCU::setLastSOPInstanceUID(const UID& instUID)
	{
		lastSOPInstanceUID_ = instUID;
		hasLastSOPInstanceUID_ = true;
	}

	void NSCU::clearLastSOPInstanceUID()
	{
		lastSOPInstanceUID_ = UID("");
		hasLastSOPInstanceUID_ = false;
	}

	void NSCU::setLastEventTypeID(UINT16 eventTypeID)
	{
		lastEventTypeID_ = eventTypeID;
		hasLastEventTypeID_ = true;
	}

	void NSCU::clearLastEventTypeID()
	{
		lastEventTypeID_ = 0;
		hasLastEventTypeID_ = false;
	}

	void NSCU::setLastActionTypeID(UINT16 actionTypeID)
	{
		lastActionTypeID_ = actionTypeID;
		hasLastActionTypeID_ = true;
	}

	void NSCU::clearLastActionTypeID()
	{
		lastActionTypeID_ = 0;
		hasLastActionTypeID_ = false;
	}

	void NSCU::readRSP(
		UINT16& status,
		DataSet& response,
		DataSet& data,
		Command::Code expectedCommand)
	{
		UINT16 dataSetType = 0;
		response = DataSet();
		data = DataSet();
		if(!service_.Read(response))
			throw exception("Unexpected association release while reading N-DIMSE response command");
		ValidateNdimseResponse(
			response,
			expectedCommand,
			lastMessageID_,
			classUID_,
			hasLastSOPInstanceUID_ ? &lastSOPInstanceUID_ : 0);
		response(TAG_DATA_SET_TYPE) >> dataSetType;
		response(TAG_STATUS) >> status;
		ValidateNdimseResponseStatus(status,expectedCommand);
		if(expectedCommand == Command::N_GET_RSP &&
			IsNdimseSuccessStatus(status) &&
			dataSetType == DataSetStatus::NO_DATA_SET)
			throw exception("N-GET success response requires an Attribute List data set");
		if(expectedCommand == Command::N_DELETE_RSP &&
			dataSetType != DataSetStatus::NO_DATA_SET)
			throw exception("N-DELETE response shall not include a data set");
		if(expectedCommand == Command::N_CREATE_RSP &&
			!IsNdimseSuccessStatus(status) &&
			response.exists(TAG_AFF_SOP_INST_UID) &&
			!IsNCreateDuplicateSOPInstanceStatus(status) &&
			!IsInvalidObjectInstanceStatus(status))
			throw exception("N-CREATE non-success response shall not include SOP Instance UID");
		if(expectedCommand == Command::N_CREATE_RSP &&
			(IsNCreateDuplicateSOPInstanceStatus(status) ||
				IsInvalidObjectInstanceStatus(status)) &&
			response.exists(TAG_AFF_SOP_INST_UID))
		{
			UID duplicateInstanceUID;
			response(TAG_AFF_SOP_INST_UID) >> duplicateInstanceUID;
			if(duplicateInstanceUID.str().size() == 0)
				throw exception("N-CREATE response requires non-empty SOP Instance UID");
		}
		if(expectedCommand == Command::N_CREATE_RSP &&
			IsNdimseSuccessStatus(status) &&
			!hasLastSOPInstanceUID_)
		{
			if(!response.exists(TAG_AFF_SOP_INST_UID))
				throw exception("N-CREATE success response requires SOP Instance UID");
			UID responseInstanceUID;
			response(TAG_AFF_SOP_INST_UID) >> responseInstanceUID;
			if(responseInstanceUID.str().size() == 0)
				throw exception("N-CREATE success response requires SOP Instance UID");
		}
		if((expectedCommand == Command::N_EVENT_REPORT_RSP ||
			expectedCommand == Command::N_ACTION_RSP) &&
			!IsNdimseSuccessStatus(status) &&
			!IsInvalidArgumentValueStatus(status) &&
			dataSetType != DataSetStatus::NO_DATA_SET)
			throw exception("N-DIMSE non-success response shall not include a data set");
		if(expectedCommand == Command::N_EVENT_REPORT_RSP &&
			hasLastEventTypeID_ && response.exists(TAG_EVENT_TYPE_ID))
		{
			UINT16 eventTypeID = 0;
			response(TAG_EVENT_TYPE_ID) >> eventTypeID;
			if(eventTypeID != lastEventTypeID_)
				throw exception("Unexpected N-DIMSE response Event Type ID");
		}
		if(expectedCommand == Command::N_EVENT_REPORT_RSP &&
			dataSetType != DataSetStatus::NO_DATA_SET &&
			!response.exists(TAG_EVENT_TYPE_ID))
			throw exception("N-EVENT-REPORT response data set requires Event Type ID");
		if(expectedCommand == Command::N_ACTION_RSP &&
			hasLastActionTypeID_ && response.exists(TAG_ACTION_TYPE_ID))
		{
			UINT16 actionTypeID = 0;
			response(TAG_ACTION_TYPE_ID) >> actionTypeID;
			if(actionTypeID != lastActionTypeID_)
				throw exception("Unexpected N-DIMSE response Action Type ID");
		}
		if(expectedCommand == Command::N_ACTION_RSP &&
			dataSetType != DataSetStatus::NO_DATA_SET &&
			!response.exists(TAG_ACTION_TYPE_ID))
			throw exception("N-ACTION response data set requires Action Type ID");
		if(dataSetType!=DataSetStatus::NO_DATA_SET && !service_.Read(data))
			throw exception("Unexpected association release while reading N-DIMSE response data set");
		service_.CompleteInvokedOperation(lastMessageID_);
	}

	NEventReportSCU::NEventReportSCU(ServiceBase& service, const UID& classUID)
		:NSCU(service,classUID)
	{
	}

	void NEventReportSCU::writeRQ(const UID& instUID, UINT16 eventTypeID, const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		setLastEventTypeID(eventTypeID);
		clearLastActionTypeID();
		CommandSet::NEventReportRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			eventTypeID,
			DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		service_.WriteDataSet(data,contextUID_);
		invoked.release();
	}

	void NEventReportSCU::writeRQ(const UID& instUID, UINT16 eventTypeID)
	{
		RequireSCURole(service_,classUID_);
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		setLastEventTypeID(eventTypeID);
		clearLastActionTypeID();
		CommandSet::NEventReportRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			eventTypeID,
			DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		invoked.release();
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
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NGetRQ rq(lastMessageID_,classUID_,instUID,attrList);
		service_.WriteCommand(rq,contextUID_);
		invoked.release();
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
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NSetRQ rq(lastMessageID_,classUID_,instUID);
		service_.WriteCommand(rq,contextUID_);
		service_.WriteDataSet(data,contextUID_);
		invoked.release();
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
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		setLastActionTypeID(actionTypeID);
		CommandSet::NActionRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			actionTypeID,
			DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		service_.WriteDataSet(data,contextUID_);
		invoked.release();
	}

	void NActionSCU::writeRQ(const UID& instUID, UINT16 actionTypeID)
	{
		RequireSCURole(service_,classUID_);
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		setLastActionTypeID(actionTypeID);
		CommandSet::NActionRQ rq(
			lastMessageID_,
			classUID_,
			instUID,
			actionTypeID,
			DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		invoked.release();
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
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,instUID,DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		service_.WriteDataSet(data,contextUID_);
		invoked.release();
	}

	void NCreateSCU::writeRQ(const UID& instUID)
	{
		RequireSCURole(service_,classUID_);
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,instUID,DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		invoked.release();
	}

	void NCreateSCU::writeRQ(const DataSet& data)
	{
		RequireSCURole(service_,classUID_);
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		clearLastSOPInstanceUID();
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,DataSetStatus::YES_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		service_.WriteDataSet(data,contextUID_);
		invoked.release();
	}

	void NCreateSCU::writeRQ()
	{
		RequireSCURole(service_,classUID_);
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		clearLastSOPInstanceUID();
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NCreateRQ rq(lastMessageID_,classUID_,DataSetStatus::NO_DATA_SET);
		service_.WriteCommand(rq,contextUID_);
		invoked.release();
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
		ensureNoOutstandingRequest();
		lastMessageID_ = uniq16odd();
		InvokedOperationRollback invoked(service_,lastMessageID_);
		setLastSOPInstanceUID(instUID);
		clearLastEventTypeID();
		clearLastActionTypeID();
		CommandSet::NDeleteRQ rq(lastMessageID_,classUID_,instUID);
		service_.WriteCommand(rq,contextUID_);
		invoked.release();
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
