#include "dicomlib/RLECodec.hpp"
#include "dicomlib/UIDs.hpp"

#include <cassert>

namespace
{
	void checkFrameRoundTrip8BitRGB()
	{
		const std::vector<BYTE> native = {
			1, 2, 3,
			4, 5, 6,
			7, 8, 9,
			10, 11, 12
		};

		const std::vector<BYTE> encoded = dicom::EncodeRLELosslessFrame(native, 2, 2, 3, 8);
		const std::vector<BYTE> decoded = dicom::DecodeRLELosslessFrame(encoded, 2, 2, 3, 8);
		assert(decoded == native);
	}

	void checkFrameRoundTrip16BitMono()
	{
		const std::vector<BYTE> native = {
			0x02, 0x01,
			0x04, 0x03,
			0x06, 0x05,
			0x08, 0x07
		};

		const std::vector<BYTE> encoded = dicom::EncodeRLELosslessFrame(native, 2, 2, 1, 16);
		const std::vector<BYTE> decoded = dicom::DecodeRLELosslessFrame(encoded, 2, 2, 1, 16);
		assert(decoded == native);
	}

	void checkDatasetRoundTrip()
	{
		dicom::DataSet native;
		native.Put<dicom::VR_US>(dicom::TAG_ROWS, UINT16(2));
		native.Put<dicom::VR_US>(dicom::TAG_COLUMNS, UINT16(2));
		native.Put<dicom::VR_US>(dicom::TAG_SAMPLES_PER_PX, UINT16(1));
		native.Put<dicom::VR_US>(dicom::TAG_BITS_ALLOC, UINT16(16));
		dicom::TypeFromVR<dicom::VR_OW>::Type pixels;
		pixels.push_back(0x0102);
		pixels.push_back(0x0304);
		pixels.push_back(0x0506);
		pixels.push_back(0x0708);
		native.Put<dicom::VR_OW>(dicom::TAG_PIXEL_DATA, pixels);

		dicom::DataSet encoded = dicom::EncodeRLELosslessPixelData(native);
		std::vector<dicom::Value> fragments = encoded.Values(dicom::TAG_PIXEL_DATA);
		assert(fragments.size() == 1);
		assert(fragments[0].vr() == dicom::VR_OB);

		dicom::DecodeRLELosslessPixelData(encoded);
		dicom::TypeFromVR<dicom::VR_OW>::Type decodedPixels;
		encoded(dicom::TAG_PIXEL_DATA) >> decodedPixels;
		assert(decodedPixels == pixels);
	}
}

int main()
{
	checkFrameRoundTrip8BitRGB();
	checkFrameRoundTrip16BitMono();
	checkDatasetRoundTrip();
	return 0;
}
