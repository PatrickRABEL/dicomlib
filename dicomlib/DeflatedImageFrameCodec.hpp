#ifndef DICOMLIB_DEFLATED_IMAGE_FRAME_CODEC_HPP
#define DICOMLIB_DEFLATED_IMAGE_FRAME_CODEC_HPP

#include "DataSet.hpp"

namespace dicom
{
	void DecodeDeflatedImageFramePixelData(DataSet& data);
	DataSet EncodeDeflatedImageFramePixelData(const DataSet& data);
}

#endif
