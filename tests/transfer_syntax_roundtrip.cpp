#include "dicomlib/Config.hpp"
#include "dicomlib/DataSet.hpp"
#include "dicomlib/Decoder.hpp"
#include "dicomlib/Encoder.hpp"
#include "dicomlib/TransferSyntax.hpp"
#include "dicomlib/UIDs.hpp"

#include <cassert>
#include <cstdlib>

namespace
{
	dicom::DataSet makeDataSet()
	{
		dicom::DataSet data;
		data.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		data.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.2"));
		data.Put<dicom::VR_LO>(dicom::TAG_PAT_ID, std::string("PATIENT-42"));
		data.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(2));
		data.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(2));
		data.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		data.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		data.Put<dicom::VR_UL>(dicom::TAG_FILE_INFO_GR_LEN, UINT32(0x00181063));
		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		pixels.push_back(0x0102);
		pixels.push_back(0x0304);
		pixels.push_back(0x0506);
		pixels.push_back(0x0708);
		data.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);
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
		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;

		decoded(dicom::TAG_SOP_CLASS_UID) >> sopClass;
		decoded(dicom::TAG_SOP_INST_UID) >> sopInstance;
		decoded(dicom::TAG_PAT_ID) >> patientID;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_FILE_INFO_GR_LEN) >> groupLength;
		decoded(dicom::TAG_PIXEL_DATA) >> pixels;

		assert(sopClass == dicom::CT_IMAGE_STORAGE_SOP_CLASS);
		assert(sopInstance == dicom::UID("1.2.826.0.1.3680043.10.2"));
		assert(patientID == "PATIENT-42");
		assert(rows == 2);
		assert(columns == 2);
		assert(groupLength == 0x00181063);
		assert(pixels.size() == 4);
		assert(pixels[0] == 0x0102);
		assert(pixels[1] == 0x0304);
		assert(pixels[2] == 0x0506);
		assert(pixels[3] == 0x0708);
	}

	void assertEncapsulatedFragmentPassThrough()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(2));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(2));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));

		dicom::TypeFromVR<dicom::VR_OB>::Type fragment1;
		fragment1.push_back(0xff);
		fragment1.push_back(0xd8);
		fragment1.push_back(0xff);
		fragment1.push_back(0xdb);
		dicom::TypeFromVR<dicom::VR_OB>::Type fragment2;
		fragment2.push_back(0xff);
		fragment2.push_back(0xda);
		fragment2.push_back(0x00);
		fragment2.push_back(0x0c);
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, fragment1);
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, fragment2);

		dicom::TS ts(dicom::JPEG_BASELINE_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		const std::vector<dicom::Value> fragments = decoded.Values(dicom::TAG_PIXEL_DATA);
		assert(fragments.size() == 2);
		assert(fragments[0].Get<dicom::TypeFromVR<dicom::VR_OB>::Type>() == fragment1);
		assert(fragments[1].Get<dicom::TypeFromVR<dicom::VR_OB>::Type>() == fragment2);
	}

	void assertJPEGBaselineRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.3"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels(64, 128);
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG_BASELINE_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 samplesPerPixel = 0;
		UINT16 bitsAllocated = 0;
		std::string lossyCompression;
		std::string compressionRatio;
		std::string compressionMethod;
		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_SAMPLES_PER_PX) >> samplesPerPixel;
		decoded(dicom::TAG_BITS_ALLOC) >> bitsAllocated;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION) >> lossyCompression;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION_RATIO) >> compressionRatio;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION_METHOD) >> compressionMethod;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(rows == 8);
		assert(columns == 8);
		assert(samplesPerPixel == 1);
		assert(bitsAllocated == 8);
		assert(lossyCompression == "01");
		assert(!compressionRatio.empty());
		assert(compressionMethod == "ISO_10918_1");
		assert(decodedPixels.size() == pixels.size());
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(decodedPixels[i] >= 126 && decodedPixels[i] <= 130);
	}

	void assertJPEG2000LosslessRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.4"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(12));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(11));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(UINT16(i * 17));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG2000_LOSSLESS_ONLY);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 samplesPerPixel = 0;
		UINT16 bitsAllocated = 0;
		UINT16 bitsStored = 0;
		UINT16 pixelRepresentation = 0;
		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_SAMPLES_PER_PX) >> samplesPerPixel;
		decoded(dicom::TAG_BITS_ALLOC) >> bitsAllocated;
		decoded(dicom::TAG_BITS_STORED) >> bitsStored;
		decoded(dicom::TAG_PX_REPRESENT) >> pixelRepresentation;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(rows == 8);
		assert(columns == 8);
		assert(samplesPerPixel == 1);
		assert(bitsAllocated == 16);
		assert(bitsStored == 12);
		assert(pixelRepresentation == 0);
		assert(decodedPixels == pixels);
	}

	void assertJPEG2000RoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.5"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(7));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(BYTE(i * 3));
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG2000);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 bitsAllocated = 0;
		UINT16 bitsStored = 0;
		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_BITS_ALLOC) >> bitsAllocated;
		decoded(dicom::TAG_BITS_STORED) >> bitsStored;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(rows == 8);
		assert(columns == 8);
		assert(bitsAllocated == 8);
		assert(bitsStored == 8);
		assert(decodedPixels == pixels);
	}

	void assertHTJ2KLosslessRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.10"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(12));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(11));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(UINT16((i * 43) & 0x0fff));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 bitsAllocated = 0;
		UINT16 bitsStored = 0;
		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_BITS_ALLOC) >> bitsAllocated;
		decoded(dicom::TAG_BITS_STORED) >> bitsStored;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(rows == 8);
		assert(columns == 8);
		assert(bitsAllocated == 16);
		assert(bitsStored == 12);
		assert(decodedPixels == pixels);
	}

	void assertJPEGLSLosslessRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.6"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(12));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(11));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(UINT16((i * 31) & 0x0fff));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG_LS_LOSSLESS_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 bitsAllocated = 0;
		UINT16 bitsStored = 0;
		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_BITS_ALLOC) >> bitsAllocated;
		decoded(dicom::TAG_BITS_STORED) >> bitsStored;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(rows == 8);
		assert(columns == 8);
		assert(bitsAllocated == 16);
		assert(bitsStored == 12);
		assert(decodedPixels == pixels);
	}

	void assertJPEGLSNearLosslessRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.7"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(7));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(BYTE(i * 3));
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		std::string lossyCompression;
		std::string compressionRatio;
		std::string compressionMethod;
		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION) >> lossyCompression;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION_RATIO) >> compressionRatio;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION_METHOD) >> compressionMethod;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(lossyCompression == "01");
		assert(!compressionRatio.empty());
		assert(compressionMethod == "ISO_14495_1");
		assert(decodedPixels.size() == pixels.size());
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(std::abs(int(decodedPixels[i]) - int(pixels[i])) <= DICOMLIB_JPEGLS_NEAR_LOSSLESS);
	}

	void assertJPEGXLLosslessRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.8"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(12));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(11));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(UINT16((i * 37) & 0x0fff));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG_XL_LOSSLESS_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 bitsAllocated = 0;
		UINT16 bitsStored = 0;
		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_ROWS) >> rows;
		decoded(dicom::TAG_COLUMNS) >> columns;
		decoded(dicom::TAG_BITS_ALLOC) >> bitsAllocated;
		decoded(dicom::TAG_BITS_STORED) >> bitsStored;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(rows == 8);
		assert(columns == 8);
		assert(bitsAllocated == 16);
		assert(bitsStored == 12);
		assert(decodedPixels == pixels);
	}

	void assertJPEGXLRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.9"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(7));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(BYTE(i * 3));
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::JPEG_XL_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		std::string lossyCompression;
		std::string compressionRatio;
		std::string compressionMethod;
		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION) >> lossyCompression;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION_RATIO) >> compressionRatio;
		decoded(dicom::TAG_LOSSY_IMAGE_COMPRESSION_METHOD) >> compressionMethod;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(lossyCompression == "01");
		assert(!compressionRatio.empty());
		assert(compressionMethod == "ISO_18181_1");
		assert(decodedPixels.size() == pixels.size());
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(std::abs(int(decodedPixels[i]) - int(pixels[i])) <= 8);
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

#if DICOMLIB_WITH_RLE
	assertRoundTrip(dicom::TS(dicom::RLE_LOSSLESS_TRANSFER_SYNTAX));
#endif

#if DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH
	assertEncapsulatedFragmentPassThrough();
#endif

#if DICOMLIB_WITH_JPEG
	assertJPEGBaselineRoundTrip();
#endif

#if DICOMLIB_WITH_JPEG2000
	assertJPEG2000LosslessRoundTrip();
	assertJPEG2000RoundTrip();
#endif

#if DICOMLIB_WITH_HTJ2K
	assertHTJ2KLosslessRoundTrip();
#endif

#if DICOMLIB_WITH_JPEGLS
	assertJPEGLSLosslessRoundTrip();
	assertJPEGLSNearLosslessRoundTrip();
#endif

#if DICOMLIB_WITH_JPEGXL
	assertJPEGXLLosslessRoundTrip();
	assertJPEGXLRoundTrip();
#endif

	return 0;
}
