#include "dicomlib/Config.hpp"
#include "dicomlib/DataSet.hpp"
#include "dicomlib/Decoder.hpp"
#include "dicomlib/Encoder.hpp"
#include "dicomlib/TransferSyntax.hpp"
#include "dicomlib/UIDs.hpp"

#include <cassert>

namespace
{
	dicom::DataSet makeDataSet()
	{
		dicom::DataSet data;
		data.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		data.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.2"));
		data.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("PATIENT-42"));
		data.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(512));
		data.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(256));
		data.Put<dicom::VR_UL>(dicom::TAG_FILE_INFO_GR_LEN, UINT32(0x00181063));
		return data;
	}

	void assertRoundTrip(const dicom::TS& ts)
	{
		dicom::DataSet source = makeDataSet();
		dicom::Buffer encoded(ts.isBigEndian() ? __BIG_ENDIAN : __LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::UID sopClass;
		dicom::UID sopInstance;
		std::string patientID;
		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT32 groupLength = 0;

		decoded(dicom::TAG_SOP_CLASS_UID) >> sopClass;
		decoded(dicom::TAG_SOP_INST_UID) >> sopInstance;
		decoded(dicom::TAG_PAT_ID) >> patientID;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_FILE_INFO_GR_LEN) >> groupLength;

		assert(sopClass == dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		assert(sopInstance == dicom::UID("1.2.826.0.1.3680043.10.2"));
		assert(patientID == "PATIENT-42");
		assert(rows == 512);
		assert(columns == 256);
		assert(groupLength == 0x00181063);
	}
}

int main()
{
	assertRoundTrip(dicom::TS(dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
	assertRoundTrip(dicom::TS(dicom::EXPL_VR_LE_TRANSFER_SYNTAX));

#if DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN
	assertRoundTrip(dicom::TS(dicom::EXPL_VR_BE_TRANSFER_SYNTAX));
#endif

#if DICOMLIB_WITH_ZLIB
	assertRoundTrip(dicom::TS(dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX));
#endif

	return 0;
}
