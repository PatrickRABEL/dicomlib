#ifndef SWITCH_ENDIAN_HPP_INCLUDE_GUARD_2304875234560
#define SWITCH_ENDIAN_HPP_INCLUDE_GUARD_2304875234560
#include <algorithm>
#include <type_traits>
#include <vector>
#include <unistd.h>

//!Reverses the bytes in a variable.
/*!
	It is possible that we could heavily optimise endian switching using
	the swab system call.

	(What namespace should this be in?)
*/


typedef void SwabType;

inline
void SwitchVectorEndian(std::vector<unsigned short>& data)
{
	static_assert(sizeof(unsigned short)==2, "unsigned short must be 2 bytes");

	SwabType* p_data=reinterpret_cast <SwabType*>(&data[0]);

	swab(p_data,
		p_data,
		static_cast<int>(data.size()*2));
}



/*
	May cause obscure linker errors under vc7.0, see discussion at
	http://groups.google.ca/groups?hl=en&lr=&ie=UTF-8&oe=UTF-8&threadm=4ac23acc.0301190831.34470124%40posting.google.com&rnum=20&prev=/groups%3Fq%3Dlnk1120%2Btemplate%2Bfunction%26hl%3Den%26lr%3D%26ie%3DUTF-8%26oe%3DUTF-8%26start%3D10%26sa%3DN
*/

template <typename T>
inline T SwitchEndian(T value)
{
	static_assert(std::is_arithmetic<T>::value, "SwitchEndian requires an arithmetic type");
	static_assert(!std::is_const<T>::value, "SwitchEndian cannot modify const values");
	unsigned char* b=(unsigned char*)(&value);
	std::reverse(b,b+sizeof(T));//alternatively could use swab()
	return value;
}




#endif //SWITCH_ENDIAN_HPP_INCLUDE_GUARD_2304875234560

