#ifndef SINGLETON_HPP_INCLUDE_GUARD_UO8DUBXNU9T3XBKXODGKOMCU
#define SINGLETON_HPP_INCLUDE_GUARD_UO8DUBXNU9T3XBKXODGKOMCU

namespace utility
{
	//!probably not thread safe.
	template <typename T>
	struct
	SingletonHolder
	{
		static T& instance()
		{
			static T t;
			return t;
		}
	};

	template <typename T>
	inline T&
	Singleton()
	{
		return SingletonHolder<T>::instance();
	}
}//namespace utility
#endif//SINGLETON_HPP_INCLUDE_GUARD_UO8DUBXNU9T3XBKXODGKOMCU
