#ifndef DICOMLIB_ENCAPSULATED_UNCOMPRESSED_CODEC_HPP
#define DICOMLIB_ENCAPSULATED_UNCOMPRESSED_CODEC_HPP

#include "DataSet.hpp"

namespace dicom
{
	void DecodeEncapsulatedUncompressedPixelData(DataSet& data);
	DataSet EncodeEncapsulatedUncompressedPixelData(const DataSet& data);
}

#endif
