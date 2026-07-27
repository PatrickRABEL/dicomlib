#include "dicomlib/Config.hpp"
#include "dicomlib/DataSet.hpp"
#include "dicomlib/Decoder.hpp"
#include "dicomlib/DeflatedImageFrameCodec.hpp"
#include "dicomlib/EncapsulatedUncompressedCodec.hpp"
#include "dicomlib/Encoder.hpp"
#include "dicomlib/JPEGCodec.hpp"
#include "dicomlib/TransferSyntax.hpp"
#include "dicomlib/UIDs.hpp"

#include <cassert>
#include <cstdlib>

#if DICOMLIB_WITH_GDCM
#include <gdcmByteValue.h>
#include <gdcmDataElement.h>
#include <gdcmImage.h>
#include <gdcmImageChangeTransferSyntax.h>
#include <gdcmPhotometricInterpretation.h>
#include <gdcmPixelFormat.h>
#include <gdcmSequenceOfFragments.h>
#include <gdcmSmartPointer.h>
#include <gdcmTransferSyntax.h>
#include <gdcmVR.h>
#endif

namespace
{
#if DICOMLIB_WITH_GDCM
	dicom::TypeFromVR<dicom::VR_OB>::Type makeGDCM8BitJPEGFragment(
		const dicom::TypeFromVR<dicom::VR_OB>::Type& pixels,
		UINT16 rows,
		UINT16 columns,
		gdcm::TransferSyntax::TSType transferSyntax)
	{
		gdcm::DataElement native(gdcm::Tag(0x7fe0, 0x0010));
		native.SetVR(gdcm::VR::OB);
		native.SetByteValue(reinterpret_cast<const char*>(pixels.data()), static_cast<uint32_t>(pixels.size()));

		gdcm::SmartPointer<gdcm::Image> image = new gdcm::Image;
		const unsigned int dimensions[3] = {columns, rows, 1};
		image->SetNumberOfDimensions(2);
		image->SetDimensions(dimensions);
		image->SetPlanarConfiguration(0);
		image->SetPhotometricInterpretation(
			gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::MONOCHROME2));
		image->SetPixelFormat(gdcm::PixelFormat(1, 8, 8, 7, 0));
		image->SetTransferSyntax(gdcm::TransferSyntax(gdcm::TransferSyntax::ExplicitVRLittleEndian));
		image->SetDataElement(native);

		gdcm::ImageChangeTransferSyntax change;
		change.SetInput(*image);
		change.SetTransferSyntax(gdcm::TransferSyntax(transferSyntax));
		if(!change.Change())
			std::abort();
		const gdcm::DataElement& encoded = change.GetOutput().GetDataElement();
		const gdcm::SequenceOfFragments* fragments = encoded.GetSequenceOfFragments();
		const gdcm::ByteValue* value = 0;
		if(fragments != 0 && fragments->GetNumberOfFragments() == 1)
			value = fragments->GetFragment(0).GetByteValue();
		else
			value = encoded.GetByteValue();
		if(value == 0)
			std::abort();
		const size_t length = static_cast<size_t>(value->GetLength());
		const char* pointer = value->GetPointer();
		if(length != 0 && pointer == 0)
			std::abort();

		dicom::TypeFromVR<dicom::VR_OB>::Type fragment(length, 0);
		for(size_t i=0;i<length;++i)
			fragment[i] = static_cast<BYTE>(pointer[i]);
		if(fragment.size() & 1)
			fragment.push_back(0);
		return fragment;
	}

	dicom::TypeFromVR<dicom::VR_OB>::Type makeGDCMJPEGExtended12BitFragment(
		const dicom::TypeFromVR<dicom::VR_OW>::Type& pixels,
		UINT16 rows,
		UINT16 columns)
	{
		gdcm::DataElement native(gdcm::Tag(0x7fe0, 0x0010));
		native.SetVR(gdcm::VR::OW);
		native.SetByteValue(
			reinterpret_cast<const char*>(pixels.data()),
			static_cast<uint32_t>(pixels.size() * sizeof(UINT16)));

		gdcm::SmartPointer<gdcm::Image> image = new gdcm::Image;
		const unsigned int dimensions[3] = {columns, rows, 1};
		image->SetNumberOfDimensions(2);
		image->SetDimensions(dimensions);
		image->SetPlanarConfiguration(0);
		image->SetPhotometricInterpretation(
			gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::MONOCHROME2));
		image->SetPixelFormat(gdcm::PixelFormat(1, 16, 12, 11, 0));
		image->SetTransferSyntax(gdcm::TransferSyntax(gdcm::TransferSyntax::ExplicitVRLittleEndian));
		image->SetDataElement(native);

		gdcm::ImageChangeTransferSyntax change;
		change.SetInput(*image);
		change.SetTransferSyntax(gdcm::TransferSyntax(gdcm::TransferSyntax::JPEGExtendedProcess2_4));
		if(!change.Change())
			std::abort();
		const gdcm::DataElement& encoded = change.GetOutput().GetDataElement();
		const gdcm::SequenceOfFragments* fragments = encoded.GetSequenceOfFragments();
		const gdcm::ByteValue* value = 0;
		if(fragments != 0 && fragments->GetNumberOfFragments() == 1)
			value = fragments->GetFragment(0).GetByteValue();
		else
			value = encoded.GetByteValue();
		if(value == 0)
			std::abort();
		const size_t length = static_cast<size_t>(value->GetLength());
		const char* pointer = value->GetPointer();
		if(length != 0 && pointer == 0)
			std::abort();

		dicom::TypeFromVR<dicom::VR_OB>::Type fragment(length, 0);
		for(size_t i=0;i<length;++i)
			fragment[i] = static_cast<BYTE>(pointer[i]);
		if(fragment.size() & 1)
			fragment.push_back(0);
		return fragment;
	}

	dicom::TypeFromVR<dicom::VR_OB>::Type makeGDCMJPEGLossless16BitFragment(
		const dicom::TypeFromVR<dicom::VR_OW>::Type& pixels,
		UINT16 rows,
		UINT16 columns,
		gdcm::TransferSyntax::TSType transferSyntax)
	{
		gdcm::DataElement native(gdcm::Tag(0x7fe0, 0x0010));
		native.SetVR(gdcm::VR::OW);
		native.SetByteValue(
			reinterpret_cast<const char*>(pixels.data()),
			static_cast<uint32_t>(pixels.size() * sizeof(UINT16)));

		gdcm::SmartPointer<gdcm::Image> image = new gdcm::Image;
		const unsigned int dimensions[3] = {columns, rows, 1};
		image->SetNumberOfDimensions(2);
		image->SetDimensions(dimensions);
		image->SetPlanarConfiguration(0);
		image->SetPhotometricInterpretation(
			gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::MONOCHROME2));
		image->SetPixelFormat(gdcm::PixelFormat(1, 16, 16, 15, 0));
		image->SetTransferSyntax(gdcm::TransferSyntax(gdcm::TransferSyntax::ExplicitVRLittleEndian));
		image->SetDataElement(native);

		gdcm::ImageChangeTransferSyntax change;
		change.SetInput(*image);
		change.SetTransferSyntax(gdcm::TransferSyntax(transferSyntax));
		if(!change.Change())
			std::abort();
		const gdcm::DataElement& encoded = change.GetOutput().GetDataElement();
		const gdcm::SequenceOfFragments* fragments = encoded.GetSequenceOfFragments();
		const gdcm::ByteValue* value = 0;
		if(fragments != 0 && fragments->GetNumberOfFragments() == 1)
			value = fragments->GetFragment(0).GetByteValue();
		else
			value = encoded.GetByteValue();
		if(value == 0)
			std::abort();
		const size_t length = static_cast<size_t>(value->GetLength());
		const char* pointer = value->GetPointer();
		if(length != 0 && pointer == 0)
			std::abort();

		dicom::TypeFromVR<dicom::VR_OB>::Type fragment(length, 0);
		for(size_t i=0;i<length;++i)
			fragment[i] = static_cast<BYTE>(pointer[i]);
		if(fragment.size() & 1)
			fragment.push_back(0);
		return fragment;
	}
#endif

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

	void assertEncapsulatedUncompressedOddFrameRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.19"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(3));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		pixels.push_back(1);
		pixels.push_back(2);
		pixels.push_back(3);
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::DataSet encodedData = dicom::EncodeEncapsulatedUncompressedPixelData(source);
		const std::vector<dicom::Value> fragments = encodedData.Values(dicom::TAG_PIXEL_DATA);
		assert(fragments.size() == 1);
		const dicom::TypeFromVR<dicom::VR_OB>::Type& fragment =
			fragments[0].Get<dicom::TypeFromVR<dicom::VR_OB>::Type>();
		assert(fragment.size() == 4);
		assert(fragment[0] == 1);
		assert(fragment[1] == 2);
		assert(fragment[2] == 3);
		assert(fragment[3] == 0);

		dicom::TS ts(dicom::ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
	}

	void assertEncapsulatedUncompressedMultiframeRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.20"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(2));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(2));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_IS>(dicom::TAG_NUM_FRAMES, std::string("2"));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<8;++i)
			pixels.push_back(UINT16(0x1000 + i));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::DataSet encodedData = dicom::EncodeEncapsulatedUncompressedPixelData(source);
		const std::vector<dicom::Value> fragments = encodedData.Values(dicom::TAG_PIXEL_DATA);
		assert(fragments.size() == 2);
		assert(fragments[0].Get<dicom::TypeFromVR<dicom::VR_OB>::Type>().size() == 8);
		assert(fragments[1].Get<dicom::TypeFromVR<dicom::VR_OB>::Type>().size() == 8);

		dicom::TS ts(dicom::ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
	}

	void assertDeflatedImageFrameOneBitRoundTrip()
	{
#if DICOMLIB_WITH_ZLIB
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.23"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(1));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		pixels.push_back(0xa5);
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::DataSet encodedData = dicom::EncodeDeflatedImageFramePixelData(source);
		const std::vector<dicom::Value> fragments = encodedData.Values(dicom::TAG_PIXEL_DATA);
		assert(fragments.size() == 1);
		assert(fragments[0].vr() == dicom::VR_OB);
		assert((fragments[0].Get<dicom::TypeFromVR<dicom::VR_OB>::Type>().size() & 1) == 0);

		dicom::TS ts(dicom::DEFLATED_IMAGE_FRAME_COMPRESSION_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
#endif
	}

	void assertDeflatedImageFrameMultiframeRoundTrip()
	{
#if DICOMLIB_WITH_ZLIB
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.24"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(2));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(2));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_IS>(dicom::TAG_NUM_FRAMES, std::string("2"));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<8;++i)
			pixels.push_back(UINT16(0x2000 + i));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::DataSet encodedData = dicom::EncodeDeflatedImageFramePixelData(source);
		const std::vector<dicom::Value> fragments = encodedData.Values(dicom::TAG_PIXEL_DATA);
		assert(fragments.size() == 2);
		assert(fragments[0].vr() == dicom::VR_OB);
		assert(fragments[1].vr() == dicom::VR_OB);

		dicom::TS ts(dicom::DEFLATED_IMAGE_FRAME_COMPRESSION_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
#endif
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

#if DICOMLIB_WITH_GDCM
	void assertGDCMJPEGExtendedProcess24RoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.19"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_CS>(dicom::TAG_PHOTOMETRIC, std::string("MONOCHROME2"));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(12));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(11));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels(64, 1024);
		source.Put<dicom::VR_OB>(
			dicom::TAG_PIXEL_DATA,
			makeGDCMJPEGExtended12BitFragment(pixels, 8, 8));

		dicom::TS ts(dicom::JPEG_EXTENDED_PROCESS_2_4_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels.size() == pixels.size());
		assert(decodedPixels[0] > 0);
		assert(decodedPixels[0] <= 0x0fff);
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(decodedPixels[i] == decodedPixels[0]);
	}

	void assertGDCMRetiredJPEG8BitRoundTrip(const dicom::UID& transferSyntaxUID, gdcm::TransferSyntax::TSType gdcmTransferSyntax)
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.22"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_CS>(dicom::TAG_PHOTOMETRIC, std::string("MONOCHROME2"));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(7));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels(64, 128);
		source.Put<dicom::VR_OB>(
			dicom::TAG_PIXEL_DATA,
			makeGDCM8BitJPEGFragment(pixels, 8, 8, gdcmTransferSyntax));

		dicom::TS ts(transferSyntaxUID);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels.size() == pixels.size());
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(decodedPixels[i] >= 120 && decodedPixels[i] <= 136);
	}

	void assertGDCMJPEGExtendedProcess35RoundTrip()
	{
		assertGDCMRetiredJPEG8BitRoundTrip(
			dicom::JPEG_EXTENDED_PROCESS_3_5_TRANSFER_SYNTAX,
			gdcm::TransferSyntax::JPEGExtendedProcess3_5);
	}

	void assertGDCMJPEGSpectralSelectionProcess68RoundTrip()
	{
		assertGDCMRetiredJPEG8BitRoundTrip(
			dicom::JPEG_SPECTRAL_SELECTION_PROCESS_6_8_TRANSFER_SYNTAX,
			gdcm::TransferSyntax::JPEGSpectralSelectionProcess6_8);
	}

	void assertGDCMJPEGFullProgressionProcess1012RoundTrip()
	{
		assertGDCMRetiredJPEG8BitRoundTrip(
			dicom::JPEG_FULL_PROGRESSION_PROCESS_10_12_TRANSFER_SYNTAX,
			gdcm::TransferSyntax::JPEGFullProgressionProcess10_12);
	}

	void assertGDCMJPEGLosslessRoundTrip(const dicom::UID& transferSyntaxUID, gdcm::TransferSyntax::TSType gdcmTransferSyntax)
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.18"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_CS>(dicom::TAG_PHOTOMETRIC, std::string("MONOCHROME2"));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(7));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(BYTE((i * 5) & 0xff));
		source.Put<dicom::VR_OB>(
			dicom::TAG_PIXEL_DATA,
			makeGDCM8BitJPEGFragment(pixels, 8, 8, gdcmTransferSyntax));

		dicom::TS ts(transferSyntaxUID);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
	}

	void assertGDCMJPEGLosslessProcess14RoundTrip()
	{
		assertGDCMJPEGLosslessRoundTrip(
			dicom::JPEG_LOSSLESS_PROCESS_14_TRANSFER_SYNTAX,
			gdcm::TransferSyntax::JPEGLosslessProcess14);
	}

	void assertGDCMJPEGLosslessProcess14SV1RoundTrip()
	{
		assertGDCMJPEGLosslessRoundTrip(
			dicom::JPEG_LOSSLESS_NON_HIERARCHICAL,
			gdcm::TransferSyntax::JPEGLosslessProcess14_1);
	}

	void assertGDCMJPEGLosslessProcess14SV116BitRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.20"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_CS>(dicom::TAG_PHOTOMETRIC, std::string("MONOCHROME2"));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(15));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(UINT16(0x1000 + i * 17));
		source.Put<dicom::VR_OB>(
			dicom::TAG_PIXEL_DATA,
			makeGDCMJPEGLossless16BitFragment(
				pixels, 8, 8, gdcm::TransferSyntax::JPEGLosslessProcess14_1));

		dicom::TS ts(dicom::JPEG_LOSSLESS_NON_HIERARCHICAL);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(source, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
	}
#endif

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

	void assertHTJ2KRPCLLosslessRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.11"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(12));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(11));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(UINT16((i * 47) & 0x0fff));
		source.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::TS ts(dicom::HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX);
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

	void assertHTJ2KRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.12"));
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

		dicom::TS ts(dicom::HTJ2K_TRANSFER_SYNTAX);
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
		assert(compressionMethod == "ISO_15444_15");
		assert(decodedPixels.size() == pixels.size());
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(std::abs(int(decodedPixels[i]) - int(pixels[i])) <= 8);
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

	void assertJPEGXLJPEGRecompressionRoundTrip()
	{
		dicom::DataSet source;
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_CLASS_UID, dicom::SC_IMAGE_STORAGE_SOP_CLASS);
		source.Put<dicom::VR_UI>(dicom::TAG_SOP_INST_UID, dicom::UID("1.2.826.0.1.3680043.10.17"));
		source.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_BITS_STORED, UINT16(8));
		source.Put<dicom::VR_US>(dicom::TAG_HIGH_BIT, UINT16(7));
		source.Put<dicom::VR_US>(dicom::TAG_PX_REPRESENT, UINT16(0));

		dicom::TypeFromVR<dicom::VR_OB>::Type pixels;
		for(size_t i=0;i<64;++i)
			pixels.push_back(BYTE(64 + i));
		source.Put<dicom::VR_OB>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::DataSet jpegSource = dicom::EncodeJPEGBaselinePixelData(source);
		dicom::TS ts(dicom::JPEG_XL_JPEG_RECOMPRESSION_TRANSFER_SYNTAX);
		dicom::Buffer encoded(__LITTLE_ENDIAN);
		dicom::WriteToBuffer(jpegSource, encoded, ts);

		dicom::DataSet decoded;
		dicom::ReadFromBuffer(encoded, decoded, ts);

		dicom::TypeFromVR<dicom::VR_OB>::Type decodedPixels;
		decoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;

		assert(decodedPixels.size() == pixels.size());
		for(size_t i=0;i<decodedPixels.size();++i)
			assert(std::abs(int(decodedPixels[i]) - int(pixels[i])) <= 8);
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
	assertEncapsulatedUncompressedOddFrameRoundTrip();
	assertEncapsulatedUncompressedMultiframeRoundTrip();

#if DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN
	assertRoundTrip(dicom::TS(dicom::EXPL_VR_BE_TRANSFER_SYNTAX));
#endif

#if DICOMLIB_WITH_ZLIB
	assertRoundTrip(dicom::TS(dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX));
	assertDeflatedImageFrameOneBitRoundTrip();
	assertDeflatedImageFrameMultiframeRoundTrip();
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

#if DICOMLIB_WITH_GDCM
	assertGDCMJPEGExtendedProcess24RoundTrip();
	assertGDCMJPEGExtendedProcess35RoundTrip();
	assertGDCMJPEGSpectralSelectionProcess68RoundTrip();
	assertGDCMJPEGFullProgressionProcess1012RoundTrip();
	assertGDCMJPEGLosslessProcess14RoundTrip();
	assertGDCMJPEGLosslessProcess14SV1RoundTrip();
	assertGDCMJPEGLosslessProcess14SV116BitRoundTrip();
#endif

#if DICOMLIB_WITH_JPEG2000
	assertJPEG2000LosslessRoundTrip();
	assertJPEG2000RoundTrip();
#endif

#if DICOMLIB_WITH_HTJ2K
	assertHTJ2KLosslessRoundTrip();
	assertHTJ2KRPCLLosslessRoundTrip();
	assertHTJ2KRoundTrip();
#endif

#if DICOMLIB_WITH_JPEGLS
	assertJPEGLSLosslessRoundTrip();
	assertJPEGLSNearLosslessRoundTrip();
#endif

#if DICOMLIB_WITH_JPEGXL
	assertJPEGXLLosslessRoundTrip();
	assertJPEGXLRoundTrip();
#if DICOMLIB_WITH_JPEG
	assertJPEGXLJPEGRecompressionRoundTrip();
#endif
#endif

	return 0;
}
