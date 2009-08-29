#include <exception>
#include <string>

namespace utility
{
	//!root exception class
	/*!
		Microsoft's library differs from the standard in that it provides
		a constructor that takes a a string argument.
		To make our libraries cross platform, we provide our own base 
		exception class that follows Microsoft's approach.
		Use this in place of std::exception and we should be OK.
	*/
	class exception : public std::exception
	{
	protected:

		 std::string what_;
	public:
	
		//!no reference, just a description
		exception(std::string what): what_(what)
		{}

		//!g++ needs this
		virtual ~exception()  throw(){}
		
		virtual const char* what() const throw()
		{
			return what_.c_str();
		}
	};
}