#ifndef DICOMLIB_JPEGXL_CODEC_HPP
#define DICOMLIB_JPEGXL_CODEC_HPP

#include "DataSet.hpp"

namespace dicom
{
	void DecodeJPEGXLLosslessPixelData(DataSet& data);
	void DecodeJPEGXLJPEGRecompressionPixelData(DataSet& data);
	void DecodeJPEGXLPixelData(DataSet& data);
	DataSet EncodeJPEGXLLosslessPixelData(const DataSet& data);
	DataSet EncodeJPEGXLJPEGRecompressionPixelData(const DataSet& data);
	DataSet EncodeJPEGXLPixelData(const DataSet& data);
}

#endif
