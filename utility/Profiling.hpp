#ifndef PROFILING_HPP_INCLUDE_GUARD_4620784204787438
#define PROFILING_HPP_INCLUDE_GUARD_4620784204787438

#include <string>
#include <time.h>
#include "Singleton.hpp"
#include "ProfilingStatistics.hpp"
/*
Simple class for quick and dirty profiling of time intensive functions.
*/


namespace utility
{
	//!instances of	this class record how long they exist for, keeping records in an instance of profile_statistics
	/*!
	putting this at the beginning of a given scope (e.g. the beginning of a function)
	will cause an entry to be added to the profiling report created
	when the application ends.

	USAGE:

	MyClass::LongFunction()
	{
	ScopedProfiler s(MyClass::LongFunction)
	//...time-intensive bit of code...//
	}

	the output  will contain a line like:

	MyClass::LongFunction() : : Average time:105.556, called 18 times, total time: 1900


	*/


	class ScopedProfiler
	{
		clock_t clock_;
		std::string name_;
	public:
		ScopedProfiler(std::string name)
			:clock_(clock())
			,name_(name)
		{
		}
		~ScopedProfiler()
		{
			clock_=clock()-clock_;//how many tick counts since this object was created.
			Singleton<ProfilingStatistics>().AddEntry(name_,clock_);
		}
	};

	inline void PrintProfilingStatistics(std::ostream& out)
	{
		Singleton<ProfilingStatistics>().PrintReport(out);
	}
}//namespace utility
#endif//PROFILING_HPP_INCLUDE_GUARD_4620784204787438
