/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/

#include <algorithm>
#include "TransferSyntax.hpp"
#include "UID.hpp"
#include "UIDs.hpp"
#include "Exceptions.hpp"
#include "dicomlib/Config.hpp"
#include <sstream>
namespace dicom
{


	TS::TS(const UID& uid):uid_(uid)
	{
		//make sure uid represents a known transfer syntax.
		dicom::Enforce(IsTransferSyntaxUID(uid),"Syntax not recognised: " + uid.str());

	}

	UID TS::getUID() const
	{
		return uid_;
	}


	/*!
		Part 5, Annex 4 (a) says we use Explicit VR, Little endian for jpeg encoded syntaxs.
	*/

	bool TS::isExplicitVR() const
	{
		return (IMPL_VR_LE_TRANSFER_SYNTAX!=uid_);
	}

	bool TS::isBigEndian() const
	{
		return (EXPL_VR_BE_TRANSFER_SYNTAX==uid_);
	}

	bool TS::isDeflated() const
	{
		return (DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX==uid_);
	}

	/*!
		Does this transfer syntax indicate that pixel data is stored
		in encapsulated encoded form, as described in Part 5 annex 4?
	*/
	bool TS::isEncapsulated() const
	{
		return IsEncapsulatedTransferSyntaxUID(uid_);
	}

	bool TS::isNativeUncompressed() const
	{
		return uid_ == IMPL_VR_LE_TRANSFER_SYNTAX ||
			uid_ == EXPL_VR_LE_TRANSFER_SYNTAX ||
			uid_ == EXPL_VR_BE_TRANSFER_SYNTAX;
	}

	bool TS::canDecodeDataset() const
	{
		if(uid_ == IMPL_VR_LE_TRANSFER_SYNTAX || uid_ == EXPL_VR_LE_TRANSFER_SYNTAX)
			return true;
#if DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN
		if(uid_ == EXPL_VR_BE_TRANSFER_SYNTAX)
			return true;
#endif
#if DICOMLIB_WITH_ZLIB
		if(uid_ == DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX)
			return true;
#endif
		return false;
	}

	bool TS::canPassThroughPixelData() const
	{
#if DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH
		if(isEncapsulated() && !isDeflated())
			return true;
#endif
		return false;
	}

	bool TS::hasCompiledPixelCodec() const
	{
#if DICOMLIB_WITH_RLE
		if(uid_ == RLE_LOSSLESS_TRANSFER_SYNTAX)
			return true;
#endif
#if DICOMLIB_WITH_JPEG
		if(uid_ == JPEG_BASELINE_TRANSFER_SYNTAX)
			return true;
#endif
		return false;
	}
}//namespace dicom
