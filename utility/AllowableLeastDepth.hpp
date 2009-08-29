#ifndef ALLOWABLE_LEAST_DEPTH_HPP_INCLUDE_GUARD_YUIGB78647KLJ
#define ALLOWABLE_LEAST_DEPTH_HPP_INCLUDE_GUARD_YUIGB78647KLJ

#include <algorithm>
#include "ImageDisplay/Matrix.hpp"
#include "utility/Enforce.hpp"


namespace utility
{
	//TEMPLATE FUNCTION DepthFromMaxValue
	template<class Type> inline
	int AllowableLeastDepth(ImageDisplay::Matrix<Type>& matrix)
	{	
		ImageDisplay::Matrix<Type>::iterator it = std::max_element(matrix.begin(), matrix.end());
		double MaxValue = (double)(*it);

		Enforce(MaxValue>0, "Matrix data is empty or all negative");
		int bit = ceil(log(MaxValue+1)/log(2.0));//plus 1 means: for value 1024 depth should be 11

		return bit<8?8:bit;
	}

	//TEMPLATE FUNCTION DepthFromMaxValue
	template<class Type> inline
	int RealAllowableLeastDepth(ImageDisplay::Matrix<Type>& matrix)
	{	
		ImageDisplay::Matrix<Type>::iterator it = std::max_element(matrix.begin(), matrix.end());
		double MaxValue = (double)(*it);

		Enforce(MaxValue>0, "Matrix data is empty or all negative");
		int bit = ceil(log(MaxValue+1)/log(2.0));//plus 1 means: for value 1024 depth should be 11

		return bit;
	}
}//namespace utility
#endif //ALLOWABLE_LEAST_DEPTH_HPP_INCLUDE_GUARD_YUIGB78647KLJ
