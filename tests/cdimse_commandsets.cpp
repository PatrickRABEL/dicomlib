#include "dicomlib/CommandSets.hpp"

#include <cassert>

namespace
{
	template <typename T>
	T get(const dicom::DataSet& data, const dicom::Tag& tag)
	{
		T value;
		data(tag) >> value;
		return value;
	}

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
	checkCMove();
	return 0;
}
