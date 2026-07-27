#ifndef JPEG2000_CODEC_HPP_INCLUDE_GUARD_31EAA938B08A
#define JPEG2000_CODEC_HPP_INCLUDE_GUARD_31EAA938B08A

#include "DataSet.hpp"

namespace dicom
{
	void DecodeJPEG2000PixelData(DataSet& data);
	DataSet EncodeJPEG2000LosslessPixelData(const DataSet& data);
}

#endif
