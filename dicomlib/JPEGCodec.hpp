#ifndef JPEG_CODEC_HPP_INCLUDE_GUARD_59E1C3EA638D
#define JPEG_CODEC_HPP_INCLUDE_GUARD_59E1C3EA638D

#include "DataSet.hpp"

namespace dicom
{
	void DecodeJPEGBaselinePixelData(DataSet& data);
	DataSet EncodeJPEGBaselinePixelData(const DataSet& data);
}

#endif
