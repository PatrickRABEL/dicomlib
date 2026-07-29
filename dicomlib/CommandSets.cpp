
#include "CommandSets.hpp"
namespace dicom
{
	namespace CommandSet
	{
		//////////////////////////////////////////////////////////////////////////
		//C-DIMSE commands
		//////////////////////////////////////////////////////////////////////////

		CEchoRQ::CEchoRQ(UINT16 msgID, const UID& classUID)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_ECHO_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE,DataSetStatus::NO_DATA_SET);
		}

		/*!
			Defined in Part 7, table 9.1-5
		*/

		CEchoRSP::CEchoRSP(UINT16 msgID, const UID& classUID)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_ECHO_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::NO_DATA_SET);
			this->Put<VR_US>(TAG_STATUS, Status::SUCCESS);
		}

		CStoreRQ::CStoreRQ(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 priority)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_STORE_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_PRIORITY, priority);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::YES_DATA_SET);
			this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
		}

		CStoreRQ::CStoreRQ(UINT16 msgID, const UID& classUID, const UID& instUID,
			const std::string& moveAET, UINT16 moveMsgID, UINT16 priority)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_STORE_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_PRIORITY, priority);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::YES_DATA_SET);
			this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
			this->Put<VR_AE>(TAG_MOVE_ORIG_AET,  moveAET);
			this->Put<VR_US>(TAG_MOVE_ORIG_MSG_ID, moveMsgID);
		}

		CStoreRSP::CStoreRSP(UINT16 msgID, const UID& classUID, const UID& instUID, UINT16 stat)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID); //Optional
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_STORE_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::NO_DATA_SET);
			this->Put<VR_US>(TAG_STATUS, stat);
		}

		CFindRQ::CFindRQ(UINT16 msgID, const UID& classUID, UINT16 priority)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_FIND_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_PRIORITY, priority);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::YES_DATA_SET);
		}

		CFindRSP::CFindRSP(UINT16 msgID, const UID& classUID, UINT16 stat, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_FIND_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
		}

		CCancelRQ::CCancelRQ(UINT16 msgID)
		{
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_CANCEL_RQ);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::NO_DATA_SET);
		}

		CGetRQ::CGetRQ(UINT16 msgID, const UID& classUID, UINT16 priority)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_GET_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_PRIORITY, priority);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::YES_DATA_SET);
		}

		CGetRSP::CGetRSP(UINT16 msgID, const UID& classUID, UINT16 stat, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_GET_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
		}

		void CGetRSP::setRemaining(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_REMAIN_SUBOP, n);
		}

		void CGetRSP::setCompleted(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_COMPL_SUBOP, n);
		}

		void CGetRSP::setFailed(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_FAIL_SUBOP, n);
		}

		void CGetRSP::setWarning(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_WARN_SUBOP, n);
		}

		CMoveRQ::CMoveRQ(UINT16 msgID, const UID& classUID, const std::string& destAET,
			UINT16 priority)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_MOVE_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_PRIORITY, priority);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::YES_DATA_SET);
			this->Put<VR_AE>(TAG_MOVE_DEST, destAET);
		}

		CMoveRSP::CMoveRSP(UINT16 msgID, const UID& classUID, UINT16 stat, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::C_MOVE_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
		}

		void CMoveRSP::setRemaining(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_REMAIN_SUBOP, n);
		}

		void CMoveRSP::setCompleted(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_COMPL_SUBOP, n);
		}

		void CMoveRSP::setFailed(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_FAIL_SUBOP, n);
		}

		void CMoveRSP::setWarning(UINT16 n)
		{
			this->Put<VR_US>(TAG_NUM_WARN_SUBOP, n);
		}


		//////////////////////////////////////////////////////////////////////////
		//N-DIMSE commands
		//////////////////////////////////////////////////////////////////////////

		namespace
		{
			void PutAttributeIdentifierList(DataSet& command, const std::vector<Tag>& attrList)
			{
				for(std::vector<Tag>::const_iterator I=attrList.begin(); I!=attrList.end(); ++I)
					command.Put<VR_AT>(TAG_ATTR_ID_LIST, *I);
			}
		}

		/*!
			Defined in Part7, table 10.3-1
		*/

		NEventReportRQ::NEventReportRQ(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 eventTypID, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID,classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_EVENT_REPORT_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
			this->Put<VR_US>(TAG_EVENT_TYPE_ID, eventTypID);

		}

		NEventReportRSP::NEventReportRSP(UINT16 msgID, const UID& classUID, UINT16 stat,
			UINT16 eventTypID, UINT16 dsType)
			:NEventReportRSP(msgID,classUID,UID(""),stat,eventTypID,dsType)
		{
		}

		NEventReportRSP::NEventReportRSP(UINT16 msgID, const UID& classUID,
			const UID& instUID, UINT16 stat, UINT16 eventTypID, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_EVENT_REPORT_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
			if(instUID.str().size()!=0)
				this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
			if (stat == Status::SUCCESS)
				this->Put<VR_US>(TAG_EVENT_TYPE_ID, eventTypID);
		}

		NGetRQ::NGetRQ(UINT16 msgID, const UID& classUID, const UID& instUID,
			const std::vector<Tag>& attrList)
		{
			this->Put<VR_UI>(TAG_REQ_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_GET_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::NO_DATA_SET);
			this->Put<VR_UI>(TAG_REQ_SOP_INST_UID,  instUID);
			PutAttributeIdentifierList(*this,attrList);
		}

		NGetRSP::NGetRSP(UINT16 msgID, const UID& classUID, UINT16 stat, UINT16 dsType)
			:NGetRSP(msgID,classUID,UID(""),stat,dsType)
		{
		}

		NGetRSP::NGetRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_GET_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
			if(instUID.str().size()!=0)
				this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
		}

		NGetRSP::NGetRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat, UINT16 dsType, const std::vector<Tag>& attrList)
			:NGetRSP(msgID,classUID,instUID,stat,dsType)
		{
			PutAttributeIdentifierList(*this,attrList);
		}

		NSetRQ::NSetRQ(UINT16 msgID, const UID& classUID, const UID& instUID)
		{
			this->Put<VR_UI>(TAG_REQ_SOP_CLASS_UID,classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_SET_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::YES_DATA_SET);
			this->Put<VR_UI>(TAG_REQ_SOP_INST_UID,instUID);
		}

		NSetRSP::NSetRSP(UINT16 msgID, const UID& classUID, UINT16 stat, UINT16 dsType)
			:NSetRSP(msgID,classUID,UID(""),stat,dsType)
		{
		}

		NSetRSP::NSetRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_SET_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
			if(instUID.str().size()!=0)
				this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
		}

		NSetRSP::NSetRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat, UINT16 dsType, const std::vector<Tag>& attrList)
			:NSetRSP(msgID,classUID,instUID,stat,dsType)
		{
			PutAttributeIdentifierList(*this,attrList);
		}

		NActionRQ::NActionRQ(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 actionTypID, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_REQ_SOP_CLASS_UID,classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_ACTION_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_UI>(TAG_REQ_SOP_INST_UID,instUID);
			this->Put<VR_US>(TAG_ACTION_TYPE_ID, actionTypID);
		}

		NActionRSP::NActionRSP(UINT16 msgID, const UID& classUID, UINT16 stat,
			UINT16 actionTypID, UINT16 dsType)
			:NActionRSP(msgID,classUID,UID(""),stat,actionTypID,dsType)
		{
		}

		NActionRSP::NActionRSP(UINT16 msgID, const UID& classUID,
			const UID& instUID, UINT16 stat, UINT16 actionTypID, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_ACTION_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
			if(instUID.str().size()!=0)
				this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
			if (stat == Status::SUCCESS)
				this->Put<VR_US>(TAG_ACTION_TYPE_ID, actionTypID);
		}

		NCreateRQ::NCreateRQ(UINT16 msgID, const UID& classUID, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_CREATE_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
		}

		NCreateRQ::NCreateRQ(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_CREATE_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
		}

		NCreateRSP::NCreateRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat, UINT16 dsType)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_CREATE_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, dsType);
			this->Put<VR_US>(TAG_STATUS, stat);
			if (stat == Status::SUCCESS)
				this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
		}

		NCreateRSP::NCreateRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat, UINT16 dsType, const std::vector<Tag>& attrList)
			:NCreateRSP(msgID,classUID,instUID,stat,dsType)
		{
			PutAttributeIdentifierList(*this,attrList);
		}

		NDeleteRQ::NDeleteRQ(UINT16 msgID, const UID& classUID, const UID& instUID)
		{
			this->Put<VR_UI>(TAG_REQ_SOP_CLASS_UID,classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_DELETE_RQ);
			this->Put<VR_US>(TAG_MSG_ID, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::NO_DATA_SET);
			this->Put<VR_UI>(TAG_REQ_SOP_INST_UID,instUID);
		}

		NDeleteRSP::NDeleteRSP(UINT16 msgID, const UID& classUID, UINT16 stat)
			:NDeleteRSP(msgID,classUID,UID(""),stat)
		{
		}

		NDeleteRSP::NDeleteRSP(UINT16 msgID, const UID& classUID, const UID& instUID,
			UINT16 stat)
		{
			this->Put<VR_UI>(TAG_AFF_SOP_CLASS_UID, classUID);
			this->Put<VR_US>(TAG_CMD_FIELD, Command::N_DELETE_RSP);
			this->Put<VR_US>(TAG_MSG_ID_RSP, msgID);
			this->Put<VR_US>(TAG_DATA_SET_TYPE, DataSetStatus::NO_DATA_SET);
			this->Put<VR_US>(TAG_STATUS, stat);
			if(instUID.str().size()!=0)
				this->Put<VR_UI>(TAG_AFF_SOP_INST_UID, instUID);
		}


	}//namespace CommandSet
}//namespace dicom
