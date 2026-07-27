#ifndef JPIP_REFERENCED_CODEC_HPP_INCLUDE_GUARD_20260728
#define JPIP_REFERENCED_CODEC_HPP_INCLUDE_GUARD_20260728

#include "DataSet.hpp"
#include "UID.hpp"

namespace dicom
{
	void ValidateJPIPReferencedDataSet(const DataSet& data, const UID& transferSyntaxUID);
}

#endif
