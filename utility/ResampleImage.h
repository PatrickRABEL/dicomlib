#pragma once
#ifndef RESAMPLE_IMAGE_H_GUARD_7846593
#define RESAMPLE_IMAGE_H_GUARD_7846593

#include <boost/lexical_cast.hpp>

#include "dicomlib/DataSet.hpp"
#include "ImageDisplay/Matrix.hpp"
#include "ImageDisplay/MatrixFromDataset.hpp"


namespace utility
{
	double PixelSpacingFromHeader(const dicom::DataSet& data);

	ImageDisplay::Matrix<unsigned short> ResampleImage(dicom::DataSet& data);
}

#endif