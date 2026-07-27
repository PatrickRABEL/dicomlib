#ifndef DICOMLIB_GDCM_JPEG_CODEC_HPP
#define DICOMLIB_GDCM_JPEG_CODEC_HPP

#include "DataSet.hpp"
#include "UID.hpp"

namespace dicom
{
	void DecodeGDCMJPEGPixelData(DataSet& data, const UID& transferSyntaxUID);
}

#endif
