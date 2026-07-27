#ifndef CPP_TYPES_HPP_INCLUDE_GUARD_74749238
#define CPP_TYPES_HPP_INCLUDE_GUARD_74749238
#include <cstdint>
/* C++ Types */
/*

	Use this file to specify fixed-length types.
	(Note that c++ defines relationships between type sizes,
	e.g. sizeof(short) <= sizeof(long), but does NOT define
	a byte count for them.  DICOM, however, specifies a fixed
	byte length for it's fundamental types, so we can avoid
	ambiguities by using guaranteed length types like UINT32
	rather than unsigned int.
*/


	typedef std::uint8_t	UINT8;
	typedef std::uint16_t	UINT16;
	typedef std::uint32_t	UINT32;
	typedef std::int32_t	INT32;
	typedef std::uint64_t	UINT64;
	typedef std::int64_t	INT64;

typedef UINT8 BYTE;


static_assert(sizeof(UINT8)==1, "UINT8 must be 1 byte");
static_assert(sizeof(UINT16)==2, "UINT16 must be 2 bytes");
static_assert(sizeof(UINT32)==4, "UINT32 must be 4 bytes");
static_assert(sizeof(INT32)==4, "INT32 must be 4 bytes");
static_assert(sizeof(UINT64)==8, "UINT64 must be 8 bytes");
static_assert(sizeof(INT64)==8, "INT64 must be 8 bytes");


#endif //CPP_TYPES_HPP_INCLUDE_GUARD_74749238
