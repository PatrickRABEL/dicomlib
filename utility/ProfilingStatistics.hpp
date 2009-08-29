#pragma once
#include <map>
#include <string>
#include <sstream>
#include <algorithm>
#include <boost/noncopyable.hpp>
namespace utility
{

	struct ProfilingStatistics:boost::noncopyable
	{
		class routine
		{
			unsigned long times_called_;
			unsigned long total_time_;

		public:
			routine ():times_called_(0),total_time_(0){}

			void AddInstance(unsigned long time)
			{
				times_called_++;
				total_time_+=time;
			}

			double			MeanTime()		const {return (double( total_time_))/times_called_;}
			double  		TotalSecs()		const {return double(total_time_)/CLOCKS_PER_SEC;}
			double 			MeanSecs()		const {return MeanTime()/CLOCKS_PER_SEC;}

			std::string Statistics ()const
			{
				std::ostringstream s;
				s << "Average time:" << MeanSecs() << " seconds, called " << times_called_
					<<" times, total time: " << TotalSecs() << " seconds." <<  std::endl;
				return s.str();
			}
		};

		std::map<std::string,routine> monitored_routines;

	public:

		struct WriteReportEntry
		{
			std::ostream& out_;
			WriteReportEntry(std::ostream& out):out_(out){}
			void operator()(const std::pair<std::string,routine>& entry){
				out_ << entry.first << " : " << entry.second.Statistics() << std::endl;
			}
		};

		/*!
		for every function that requested a profile, report the number of calls,
		total time and average time spent in that function.
		write the report to a file.
		*/


		void PrintReport(std::ostream& out)
		{
			std::for_each(monitored_routines.begin(),monitored_routines.end(),
				WriteReportEntry(out));
		}

		void AddEntry(std::string name, unsigned long time)
		{
			monitored_routines[name].AddInstance(time);
		}
	};
}//namespace utility