#ifndef RLE_CODEC_HPP_INCLUDE_GUARD_6827A5B8A9C4
#define RLE_CODEC_HPP_INCLUDE_GUARD_6827A5B8A9C4

#include "DataSet.hpp"
#include "Types.hpp"

#include <vector>

namespace dicom
{
	std::vector<BYTE> DecodeRLELosslessFrame(
		const std::vector<BYTE>& encodedFrame,
		UINT16 rows,
		UINT16 columns,
		UINT16 samplesPerPixel,
		UINT16 bitsAllocated);

	std::vector<BYTE> EncodeRLELosslessFrame(
		const std::vector<BYTE>& nativeFrame,
		UINT16 rows,
		UINT16 columns,
		UINT16 samplesPerPixel,
		UINT16 bitsAllocated);

	void DecodeRLELosslessPixelData(DataSet& data);
	DataSet EncodeRLELosslessPixelData(const DataSet& data);
}

#endif
