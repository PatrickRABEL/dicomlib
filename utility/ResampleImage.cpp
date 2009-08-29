#include <list>

#include "ResampleImage.h"
#include "ImageDisplay/operations.hpp"


using namespace std;
using namespace dicom;

namespace utility
{
	bool ValidatePixelSize(double pixel_size, UINT16 width, UINT16 height, int val)
	{
		if(pixel_size>1)//micron
		{
			if(abs(pixel_size*width/1000-300)<val && abs(pixel_size*height/1000-240)<val)
				return true;
			else if(abs(pixel_size*width/1000-240)<val && abs(pixel_size*height/1000-300)<val)
				return true;
			else if(abs(pixel_size*width/1000-240)<val && abs(pixel_size*height/1000-180)<val)
				return true;
			else if(abs(pixel_size*width/1000-180)<val && abs(pixel_size*height/1000-240)<val)
				return true;
		}
		else//mm, mm is the standard unit in DICOM
		{
			if(abs(pixel_size*width-300)<val && abs(pixel_size*height-240)<val)
				return true;
			else if(abs(pixel_size*width-240)<val && abs(pixel_size*height-300)<val)
				return true;
			else if(abs(pixel_size*width-240)<val && abs(pixel_size*height-180)<val)
				return true;
			else if(abs(pixel_size*width-180)<val && abs(pixel_size*height-240)<val)
				return true;
		}

		return false;
	}

	int GetResampleFactor(double pixel_size, int width, int height)
	{
		int factor = 1;
		if(pixel_size > 1)							//micron unit
		{
			if(!ValidatePixelSize(pixel_size, width, height, 25))//does 25 makes sense?
				throw std::exception("Pixel Size not valid");

			if(pixel_size >= 200)
				throw std::exception("Pixel Size big enough. Don't need down sample");
			else if(100<pixel_size && pixel_size<200)
				factor = 2;
			else if(80<pixel_size && pixel_size<=100)
				factor = 3;
			else 
				factor = 4;
		}
		else if(pixel_size < 1 /*&& pixel_size >0.03*/) //mm unit
		{
			if(!ValidatePixelSize(pixel_size, width, height, 25))//does 25 makes sense?
				throw std::exception("Pixel Size not valid");

			if(pixel_size >= 0.2)
				throw std::exception("Pixel Size big enough. Don't need down sample");
			else if(0.1<pixel_size && pixel_size<0.2)
				factor = 2;
			else if(0.08<pixel_size && pixel_size<=0.1)
				factor = 3;
			else 
				factor = 4;
		}
		else									//other cases: No Idea, just don't do it
			throw std::exception("Pixel Size not valid");

		return factor;
	}

	double PixelSpacingFromHeader(const dicom::DataSet& data)
	{
		/** try to get pixel_spacing in string format, 
		 *   and then transform it to double
		 */
		std::string pixel_spacing;
		if(data.exists(dicom::makeTag(0x0018, 0x7022)))//DetectorElementSpacing
			data(dicom::makeTag(0x0018, 0x7022)) >> pixel_spacing;
		//else if(data.exists(dicom::makeTag(0x0018, 0x7020)))//DetectorElementPhysicalSize is like film size
		//	data(dicom::makeTag(0x0018, 0x7020)) >> pixel_spacing;
		else if(data.exists(dicom::TAG_PIXEL_SPACING))
			data(dicom::TAG_PIXEL_SPACING) >> pixel_spacing;
		else if(data.exists(dicom::TAG_IMGR_PIXEL_SPACING))
			data(dicom::TAG_IMGR_PIXEL_SPACING) >> pixel_spacing;
		else if(data.exists(dicom::makeTag(0x0018,0x2010)))
			data(dicom::makeTag(0x0018,0x2010)) >> pixel_spacing;
		else
			throw std::exception("Pixel Size not found");//return;

		double pixel_size=-1.0;
		if(!pixel_spacing.empty())
		{
			std::string tokenized;
			if(pixel_spacing.rfind("\\")!=std::string::npos)
				tokenized=pixel_spacing.substr(0, pixel_spacing.rfind("\\"));
			else
				tokenized=pixel_spacing;

			try
			{
				std::replace_if(tokenized.begin(), tokenized.end(), std::bind2nd(std::equal_to<char>(), ','), '.');
				pixel_size=boost::lexical_cast<double,std::string>(tokenized);
			}
			catch(std::exception&)
			{
				throw std::exception("PixelSpacing format not correct");
			}
		}
		else
			throw std::exception("Pixel Size Value empty");

		return pixel_size;
	}

	/**down sample the image based on the image pixel spacing, 
	 * for big images scanned from Lumisys or for digital images. Nov 2006
	 */
	ImageDisplay::Matrix<unsigned short> ResampleImage(dicom::DataSet& data)
	{
		unsigned short width,height, depth, bits_allocated;
		data(TAG_COLUMNS) >> width;
		data(TAG_ROWS) >> height;
		data(TAG_BITS_STORED) >> depth;

		data(TAG_BITS_ALLOC) >> bits_allocated;
		utility::Enforce( 16==bits_allocated,"bad value for bits allocated");

		//after resample we get a processed image which shouldn't be the same as the original
		std::list <std::string> image_type; 
		image_type.push_back("DERIVED");
		image_type.push_back("SECONDARY");
		image_type.push_back(std::string("")); 
		data.erase(TAG_IMAGE_TYPE);
		for(list <string>::iterator item = image_type.begin();item!=image_type.end();item++)
			data.Put<VR_CS>(TAG_IMAGE_TYPE, *item);

		/**! Not sure if need to change SOPClassUid*/
		//dicom::UID SopClassUid=SC_IMAGE_STORAGE_SOP_CLASS;
		//data.erase(TAG_SOP_CLASS_UID);
		//data.Put<VR_UI>(TAG_SOP_CLASS_UID, SopClassUid);

		//dicom::UID SopInstUid = makeUID("1.2.826.0.1.3680043.2.1553.1");
		//data.erase(TAG_SOP_INST_UID);
		//data_set.Put<VR_UI>(TAG_SOP_INST_UID, SopInstUid);

		dicom::UID SopInstanceUID;
		if(data.exists(dicom::TAG_SOP_INST_UID))
		{
			data(dicom::TAG_SOP_INST_UID) >> SopInstanceUID;
			if(!SopInstanceUID.str().empty())
			{
				string tmp=SopInstanceUID.str() + ".2";
				SopInstanceUID=dicom::UID(tmp);
				data.erase(TAG_SOP_INST_UID);
				data.Put<VR_UI>(TAG_SOP_INST_UID, SopInstanceUID);
			}
		}

		dicom::UID StudyInstanceUID;
		dicom::UID SeriesInstanceUID;
		if(data.exists(dicom::TAG_STUDY_INST_UID))
		{
			data(dicom::TAG_STUDY_INST_UID) >> StudyInstanceUID;
			if(!StudyInstanceUID.str().empty())
			{
				string tmp=StudyInstanceUID.str() + ".2";
				StudyInstanceUID=dicom::UID(tmp);
				data.erase(TAG_STUDY_INST_UID);
				data.Put<VR_UI>(TAG_STUDY_INST_UID, StudyInstanceUID);
			}
		}
		if(data.exists(dicom::TAG_SERIES_INST_UID))
		{
			data(dicom::TAG_SERIES_INST_UID) >> SeriesInstanceUID;
			if(!SeriesInstanceUID.str().empty())
			{
				string tmp=SeriesInstanceUID.str() + ".2";
				SeriesInstanceUID=dicom::UID(tmp);
				data.erase(TAG_SERIES_INST_UID);
				data.Put<VR_UI>(TAG_SERIES_INST_UID, SeriesInstanceUID);
			}
		}	

		const dicom::DataSet ds=data;
		double pixel_size = PixelSpacingFromHeader(ds);
		int factor = GetResampleFactor(pixel_size, width, height);

		ImageDisplay::Matrix<unsigned short> in_matrix = ImageDisplay::MatrixFromDataSet2<unsigned short>(data);
		ImageDisplay::Matrix<unsigned short> out_matrix = ImageDisplay::MeanDownSample(in_matrix,factor);

		data.erase(TAG_COLUMNS);
		data.Put<VR_US>(TAG_COLUMNS,UINT16(out_matrix.Columns()));
		data.erase(TAG_ROWS);
		data.Put<VR_US>(TAG_ROWS, UINT16(out_matrix.Rows()));

		unsigned short max_val=*std::max_element(out_matrix.begin(), out_matrix.end());
		//data.erase(TAG_HIGH_BIT);
		//data.Put<VR_US>(TAG_HIGH_BIT, UINT16(out_matrix.Rows()));
		//data.erase(TAG_BITS_STORED);


		std::list<std::string> pixel_spacing;

		pixel_spacing.push_back(boost::lexical_cast<string, double>(pixel_size * factor));
		pixel_spacing.push_back(boost::lexical_cast<string, double>(pixel_size * factor));

		if(data.exists(dicom::TAG_PIXEL_SPACING))
		{
			data.erase(TAG_PIXEL_SPACING);
			for(list <string>::iterator item = pixel_spacing.begin();item!=pixel_spacing.end();item++)
				data.Put<VR_DS>(makeTag(0x0028,0x0030), *item);
		}
		if(data.exists(dicom::TAG_IMGR_PIXEL_SPACING))
		{
			data.erase(TAG_IMGR_PIXEL_SPACING);
			for(list <string>::iterator item = pixel_spacing.begin();item!=pixel_spacing.end();item++)
				data.Put<VR_DS>(TAG_IMGR_PIXEL_SPACING, *item); 
		}
		if(data.exists(dicom::makeTag(0x0018,0x2010)))
		{
			data.erase(dicom::makeTag(0x0018,0x2010));
			for(list <string>::iterator item = pixel_spacing.begin();item!=pixel_spacing.end();item++)
				data.Put<VR_DS>(makeTag(0x0018,0x2010), *item);
		}

		data.erase(TAG_PIXEL_DATA);
		std::vector<unsigned short> new_pixel_data(out_matrix.begin(), out_matrix.end());
		data.Put<VR_OW>(TAG_PIXEL_DATA, new_pixel_data);

		//get rid of detecor-related size to avoid confusion
		data.erase(TAG_FOCAL_SPOT);
		data.erase(makeTag(0x0018, 0x7020));//DetectorElementPhysicalSize
		data.erase(makeTag(0x0018, 0x7022));//DetectorElementSpacing

		return out_matrix;
	}

}