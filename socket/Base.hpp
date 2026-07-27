#ifndef BASE_HPP_INCLUDE_GUARD_537304573409
#define BASE_HPP_INCLUDE_GUARD_537304573409

/*
	Operating-system specific definitions go here.
	Supported targets are Linux and macOS on x86/x86_64 and ARM 32/64-bit.
	Mostly this file is used for detecting host endian-ness.
	If there's a cleaner way of doing this, please let me know

	Basically we're trying to make every system have the same
	definitions that linux presents in <endian.h>, that is,
	__LITTLE_ENDIAN, __BIG_ENDIAN, and __BYTE_ORDER
*/

#if defined(__linux__)
	#include <endian.h>
#elif defined(__APPLE__) && defined(__MACH__)
	#include <machine/endian.h>
	#ifndef __LITTLE_ENDIAN
		#define __LITTLE_ENDIAN LITTLE_ENDIAN
	#endif
	#ifndef __BIG_ENDIAN
		#define __BIG_ENDIAN BIG_ENDIAN
	#endif
	#ifndef __BYTE_ORDER
		#define __BYTE_ORDER BYTE_ORDER
	#endif
#else
	#error "Unsupported operating system. Supported targets are Linux and macOS."
#endif

#if defined(__i386__) || defined(__i386) || defined(__x86_64__) || defined(__x86_64) || defined(__arm__) || defined(__aarch64__) || defined(__arm64__)
	// Supported CPU architecture.
#else
	#error "Unsupported architecture. Supported targets are x86, x86_64, ARM 32-bit and ARM 64-bit."
#endif



#endif //BASE_HPP_INCLUDE_GUARD_537304573409
