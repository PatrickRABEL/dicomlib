#ifndef COPY_IF_HPP_INCLUDE_GUARD_KUNNK83X23AUOE8U3DA9U
#define COPY_IF_HPP_INCLUDE_GUARD_KUNNK83X23AUOE8U3DA9U
/*
	The STL doesn't include a copy_if algorithm, though it does have
	remove_copy_if.  (Stroustrup has noted that this was an erroneous ommission!)

	I include my own implementation of copy_if, modified from microsofts 
	remove_copy_if.
*/	

namespace utility
{
// TEMPLATE FUNCTION copy_if
template<class _InIt,
	class _OutIt,
	class _Pr> inline
	_OutIt copy_if(_InIt _First, _InIt _Last, _OutIt _Dest, _Pr _Pred)
	{	// copy each element satisfying _Pred
	for (; _First != _Last; ++_First)
		if (_Pred(*_First))
			*_Dest++ = *_First;
	return (_Dest);
	}
}//namespace utility
#endif //COPY_IF_HPP_INCLUDE_GUARD_KUNNK83X23AUOE8U3DA9U
