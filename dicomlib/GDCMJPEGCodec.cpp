#include "GDCMJPEGCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "UIDs.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_GDCM
#include <gdcmByteValue.h>
#include <gdcmDataElement.h>
#include <gdcmFragment.h>
#include <gdcmJPEGCodec.h>
#include <gdcmPhotometricInterpretation.h>
#include <gdcmPixelFormat.h>
#include <gdcmSequenceOfFragments.h>
#include <gdcmTransferSyntax.h>
#include <gdcmVR.h>

#include <string>
#endif

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_GDCM
		struct ImageGeometry
		{
			UINT16 rows;
			UINT16 columns;
			UINT16 samplesPerPixel;
			UINT16 bitsAllocated;
			UINT16 bitsStored;
			UINT16 highBit;
			UINT16 pixelRepresentation;
			UINT16 planarConfiguration;
			std::string photometricInterpretation;
		};

		ImageGeometry ReadImageGeometry(const DataSet& data)
		{
			ImageGeometry geometry = {0, 0, 0, 0, 0, 0, 0, 0, ""};
			data(TAG_ROWS) >> geometry.rows;
			data(TAG_COLUMNS) >> geometry.columns;
			data(TAG_SAMPLES_PER_PX) >> geometry.samplesPerPixel;
			data(TAG_BITS_ALLOC) >> geometry.bitsAllocated;
			data(TAG_BITS_STORED) >> geometry.bitsStored;
			data(TAG_HIGH_BIT) >> geometry.highBit;
			data(TAG_PX_REPRESENT) >> geometry.pixelRepresentation;
			data(TAG_PHOTOMETRIC) >> geometry.photometricInterpretation;
			if(geometry.samplesPerPixel > 1)
				data(TAG_PLANAR_CONFIG) >> geometry.planarConfiguration;

			Enforce(geometry.samplesPerPixel == 1 || geometry.samplesPerPixel == 3,
				"GDCM JPEG support requires 1 or 3 samples per pixel");
			Enforce(geometry.bitsAllocated == 8 || geometry.bitsAllocated == 16,
				"GDCM JPEG support requires 8-bit or 16-bit Pixel Data");
			Enforce(geometry.bitsStored > 0 && geometry.bitsStored <= geometry.bitsAllocated,
				"GDCM JPEG Bits Stored is inconsistent with Bits Allocated");
			Enforce(geometry.highBit + 1 == geometry.bitsStored,
				"GDCM JPEG High Bit is inconsistent with Bits Stored");
			Enforce(geometry.pixelRepresentation <= 1,
				"GDCM JPEG Pixel Representation must be 0 or 1");
			Enforce(geometry.samplesPerPixel == 1 || geometry.planarConfiguration == 0,
				"GDCM JPEG RGB decode support requires Planar Configuration 0");
			return geometry;
		}

		gdcm::TransferSyntax GDCMTransferSyntax(const UID& transferSyntaxUID)
		{
			if(transferSyntaxUID == JPEG_LOSSLESS_NON_HIERARCHICAL)
				return gdcm::TransferSyntax(gdcm::TransferSyntax::JPEGLosslessProcess14_1);
			throw exception("Unsupported GDCM JPEG Transfer Syntax");
		}

		gdcm::PhotometricInterpretation GDCMPhotometricInterpretation(const std::string& photometric)
		{
			if(photometric == "MONOCHROME1")
				return gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::MONOCHROME1);
			if(photometric == "MONOCHROME2")
				return gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::MONOCHROME2);
			if(photometric == "RGB")
				return gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::RGB);
			if(photometric == "YBR_FULL")
				return gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::YBR_FULL);
			if(photometric == "YBR_FULL_422")
				return gdcm::PhotometricInterpretation(gdcm::PhotometricInterpretation::YBR_FULL_422);
			throw exception("Unsupported GDCM JPEG Photometric Interpretation");
		}

		gdcm::DataElement GDCMFragmentedPixelData(const DataSet& data)
		{
			gdcm::SmartPointer<gdcm::SequenceOfFragments> sequence = gdcm::SequenceOfFragments::New();
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "GDCM JPEG Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
			{
				Enforce(fragments[i].vr() == VR_OB, "GDCM JPEG fragments must be OB");
				const std::vector<BYTE>& bytes = fragments[i].Get<TypeFromVR<VR_OB>::Type>();
				gdcm::Fragment fragment;
				fragment.SetByteValue(
					reinterpret_cast<const char*>(bytes.data()),
					static_cast<uint32_t>(bytes.size()));
				sequence->AddFragment(fragment);
			}

			gdcm::DataElement pixelData(gdcm::Tag(0x7fe0, 0x0010));
			pixelData.SetVLToUndefined();
			pixelData.SetValue(*sequence);
			pixelData.SetVR(gdcm::VR::OB);
			return pixelData;
		}

		std::vector<BYTE> ByteValueBytes(const gdcm::DataElement& element)
		{
			const gdcm::ByteValue* value = element.GetByteValue();
			Enforce(value != 0, "GDCM JPEG decoder did not produce native Pixel Data");
			const size_t length = static_cast<size_t>(value->GetLength());
			std::vector<BYTE> bytes(length, 0);
			const char* pointer = value->GetPointer();
			if(length > 0)
				Enforce(pointer != 0, "GDCM JPEG decoder produced an invalid Pixel Data buffer");
			for(size_t i=0;i<length;++i)
				bytes[i] = static_cast<BYTE>(pointer[i]);
			return bytes;
		}

		size_t ExpectedNativeSize(const ImageGeometry& geometry)
		{
			return size_t(geometry.rows) * size_t(geometry.columns) *
				size_t(geometry.samplesPerPixel) * size_t(geometry.bitsAllocated / 8);
		}
#endif
	}

	void DecodeGDCMJPEGPixelData(DataSet& data, const UID& transferSyntaxUID)
	{
#if DICOMLIB_WITH_GDCM
		const ImageGeometry geometry = ReadImageGeometry(data);
		gdcm::JPEGCodec codec;
		codec.SetDimensions(std::vector<unsigned int>{geometry.columns, geometry.rows});
		codec.SetPlanarConfiguration(geometry.planarConfiguration);
		codec.SetPhotometricInterpretation(GDCMPhotometricInterpretation(geometry.photometricInterpretation));
		codec.SetPixelFormat(gdcm::PixelFormat(
			geometry.samplesPerPixel,
			geometry.bitsAllocated,
			geometry.bitsStored,
			geometry.highBit,
			geometry.pixelRepresentation));
		codec.SetLossyFlag(GDCMTransferSyntax(transferSyntaxUID).IsLossy());
		codec.SetNeedByteSwap(false);

		gdcm::DataElement decoded(gdcm::Tag(0x7fe0, 0x0010));
		Enforce(codec.Decode(GDCMFragmentedPixelData(data), decoded),
			"Failed to decode GDCM JPEG Pixel Data");
		const std::vector<BYTE> pixels = ByteValueBytes(decoded);
		Enforce(pixels.size() == ExpectedNativeSize(geometry),
			"GDCM JPEG decoded size is inconsistent with DICOM image attributes");

		data.erase(TAG_PIXEL_DATA);
		if(geometry.bitsAllocated == 8)
			data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
		else
		{
			std::vector<UINT16> words(pixels.size() / 2, 0);
			for(size_t i=0;i<words.size();++i)
				words[i] = UINT16(pixels[i * 2]) | (UINT16(pixels[i * 2 + 1]) << 8);
			data.Put<VR_OW>(TAG_PIXEL_DATA, words);
		}
#else
		(void)data;
		(void)transferSyntaxUID;
		throw exception("GDCM JPEG requires DICOMLIB_WITH_GDCM");
#endif
	}
}
