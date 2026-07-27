#ifndef DICOMLIB_HTJ2K_CODEC_HPP
#define DICOMLIB_HTJ2K_CODEC_HPP

#include "DataSet.hpp"

namespace dicom
{
	void DecodeHTJ2KLosslessPixelData(DataSet& data);
	void DecodeHTJ2KRPCLLosslessPixelData(DataSet& data);
	DataSet EncodeHTJ2KLosslessPixelData(const DataSet& data);
	DataSet EncodeHTJ2KRPCLLosslessPixelData(const DataSet& data);
}

#endif
