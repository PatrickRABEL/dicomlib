#pragma once
#include "exception.hpp"

namespace utility
{

	struct FailedEnforcement : public utility::exception
	{
		explicit FailedEnforcement(const std::string& message)
			:utility::exception(message){}
	};
	
	//!Throw an FailedEnforcement if (condition) is not true.
	/*!
		Simplified from http://www.cuj.com/documents/s=8250/cujcexp2106alexandr/alexandr.htm
	*/

	inline void Enforce(bool Condition, std::string Description)
	{
		if(!Condition)
			throw FailedEnforcement(Description.c_str());
	}

}//namespace utility