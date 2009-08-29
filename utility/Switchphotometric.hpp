#ifndef SWITCHPHOTOMETRIC_HPP_INCLUDE_GUARD_ADGHKYT4865493FHJ7GH
#define SWITCHPHOTOMETRIC_HPP_INCLUDE_GUARD_ADGHKYT4865493FHJ7GH

#pragma once 

/*!
		Generally assume black-to-white pixel data, so if we have
		white-to-black, we need to reverse it.
*/


#include "dicomlib/DataSet.hpp"


namespace utility
{	
	template<typename IterType>
	void SwitchPhotometric(IterType first,IterType last,unsigned short depth)
	{
		unsigned short MAX= ((1 << depth)-1);
		unsigned short max_real= *(std::max_element(first, last));

		//Each element needs to be converted to (MAX - element)
		if(MAX>=max_real)
			transform(first, last, first, std::bind1st(std::minus<unsigned short>(),MAX));
		else
		{
			std::vector<int> tmp;
			tmp.assign(first, last);
			std::transform(tmp.begin(),tmp.end(),tmp.begin(), std::bind1st(std::minus<int>(),MAX));
			std::vector<unsigned short> tmp2;
			std::vector<int>::iterator it;
			for(it=tmp.begin(); it!=tmp.end(); it++)
			{
				if(*it<0)
					*it=abs(*it);
				tmp2.push_back(*it);
			}

			std::copy(tmp2.begin(), tmp2.end(), first);
		}
		
		//to be safe
		std::replace_if(first,last,
			std::bind2nd(std::greater<unsigned short>(),MAX),
			MAX);
	}

	/*!
		We need to have the chest wall on the right hand side of the
		screen, so check to see if we should rotate the image. (can this function be tidied up?)
	*/	
   inline bool ShouldFlipImage(const dicom::DataSet& ds)
	{
		if(ds.exists(dicom::TAG_SECONDARY_CAPTURE_DATE)||ds.exists(dicom::TAG_SECONDARY_CAPTURE_TIME))
			return false;

		std::string patientOrientation, strLaterality;
		if(ds.find(dicom::TAG_PATIENT_ORIENTATION)!=ds.end())
			ds(dicom::TAG_PATIENT_ORIENTATION) >> patientOrientation;
		if(ds.find(dicom::TAG_LATERALITY)!=ds.end())
			ds(dicom::TAG_LATERALITY) >> strLaterality;
		if (!patientOrientation.empty()||!strLaterality.empty())
			if (patientOrientation[0]=='P'||strLaterality[0]=='R')
				return true;
		return false;
	}

}//namespace utility

#endif//SWITCHPHOTOMETRIC_HPP_INCLUDE_GUARD_ADGHKYT4865493FHJ7GH