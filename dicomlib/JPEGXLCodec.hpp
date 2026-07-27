#ifndef DICOMLIB_JPEGXL_CODEC_HPP
#define DICOMLIB_JPEGXL_CODEC_HPP

#include "DataSet.hpp"

namespace dicom
{
	void DecodeJPEGXLLosslessPixelData(DataSet& data);
	DataSet EncodeJPEGXLLosslessPixelData(const DataSet& data);
}

#endif
