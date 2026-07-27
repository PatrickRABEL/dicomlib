#ifndef JPEGLS_CODEC_HPP_INCLUDE_GUARD_28B7744868BA
#define JPEGLS_CODEC_HPP_INCLUDE_GUARD_28B7744868BA

#include "DataSet.hpp"

namespace dicom
{
	void DecodeJPEGLSLosslessPixelData(DataSet& data);
	void DecodeJPEGLSNearLosslessPixelData(DataSet& data);
	DataSet EncodeJPEGLSLosslessPixelData(const DataSet& data);
	DataSet EncodeJPEGLSNearLosslessPixelData(const DataSet& data);
}

#endif
