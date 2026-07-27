#include "dicomlib/Config.hpp"
#include "dicomlib/PresentationContexts.hpp"
#include "dicomlib/Server.hpp"
#include "dicomlib/TransferSyntax.hpp"
#include "dicomlib/UIDs.hpp"

#include <cassert>

namespace
{
	bool hasTransferSyntax(const dicom::PresentationContexts& contexts, const dicom::UID& uid)
	{
		assert(contexts.size() == 1);
		const std::vector<dicom::primitive::TransferSyntax>& transferSyntaxes = contexts[0].TransferSyntaxes_;
		for(size_t i=0;i<transferSyntaxes.size();++i)
		{
			if(transferSyntaxes[i].UID_ == uid)
				return true;
		}
		return false;
	}

	bool serverAccepts(dicom::Server& server, const dicom::UID& uid)
	{
		dicom::primitive::TransferSyntax transferSyntax(uid);
		return server.CanHandleTransferSyntax(transferSyntax);
	}
}

int main()
{
	dicom::TS implicitLittle(dicom::IMPL_VR_LE_TRANSFER_SYNTAX);
	dicom::TS explicitLittle(dicom::EXPL_VR_LE_TRANSFER_SYNTAX);
	dicom::TS explicitBig(dicom::EXPL_VR_BE_TRANSFER_SYNTAX);
	dicom::TS deflated(dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX);
	dicom::TS jpegBaseline(dicom::JPEG_BASELINE_TRANSFER_SYNTAX);
	dicom::TS jpegLosslessProcess14SV1(dicom::JPEG_LOSSLESS_NON_HIERARCHICAL);
	dicom::TS jpegLSLossless(dicom::JPEG_LS_LOSSLESS_TRANSFER_SYNTAX);
	dicom::TS jpegLSNearLossless(dicom::JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX);
	dicom::TS jpeg2000Lossless(dicom::JPEG2000_LOSSLESS_ONLY);
	dicom::TS jpeg2000(dicom::JPEG2000);
	dicom::TS htj2kLossless(dicom::HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX);
	dicom::TS htj2kRPCLLossless(dicom::HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX);
	dicom::TS htj2k(dicom::HTJ2K_TRANSFER_SYNTAX);
	dicom::TS jpegXLLossless(dicom::JPEG_XL_LOSSLESS_TRANSFER_SYNTAX);
	dicom::TS jpegXLJPEGRecompression(dicom::JPEG_XL_JPEG_RECOMPRESSION_TRANSFER_SYNTAX);
	dicom::TS jpegXL(dicom::JPEG_XL_TRANSFER_SYNTAX);
	dicom::TS rle(dicom::RLE_LOSSLESS_TRANSFER_SYNTAX);

	assert(implicitLittle.canDecodeDataset());
	assert(explicitLittle.canDecodeDataset());
	assert(!jpegBaseline.canDecodeDataset());
	assert(jpegBaseline.isEncapsulated());
	assert(jpegLosslessProcess14SV1.isEncapsulated());
	assert(jpegLSLossless.isEncapsulated());
	assert(jpegLSNearLossless.isEncapsulated());
	assert(jpeg2000Lossless.isEncapsulated());
	assert(jpeg2000.isEncapsulated());
	assert(htj2kLossless.isEncapsulated());
	assert(htj2kRPCLLossless.isEncapsulated());
	assert(htj2k.isEncapsulated());
	assert(jpegXLLossless.isEncapsulated());
	assert(jpegXLJPEGRecompression.isEncapsulated());
	assert(jpegXL.isEncapsulated());

#if DICOMLIB_WITH_JPEG
	assert(jpegBaseline.hasCompiledPixelCodec());
#else
	assert(!jpegBaseline.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_GDCM
	assert(jpegLosslessProcess14SV1.hasCompiledPixelCodec());
#else
	assert(!jpegLosslessProcess14SV1.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_JPEGLS
	assert(jpegLSLossless.hasCompiledPixelCodec());
	assert(jpegLSNearLossless.hasCompiledPixelCodec());
#else
	assert(!jpegLSLossless.hasCompiledPixelCodec());
	assert(!jpegLSNearLossless.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_JPEG2000
	assert(jpeg2000Lossless.hasCompiledPixelCodec());
	assert(jpeg2000.hasCompiledPixelCodec());
#else
	assert(!jpeg2000Lossless.hasCompiledPixelCodec());
	assert(!jpeg2000.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_HTJ2K
	assert(htj2kLossless.hasCompiledPixelCodec());
	assert(htj2kRPCLLossless.hasCompiledPixelCodec());
	assert(htj2k.hasCompiledPixelCodec());
#else
	assert(!htj2kLossless.hasCompiledPixelCodec());
	assert(!htj2kRPCLLossless.hasCompiledPixelCodec());
	assert(!htj2k.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_JPEGXL
	assert(jpegXLLossless.hasCompiledPixelCodec());
	assert(jpegXLJPEGRecompression.hasCompiledPixelCodec());
	assert(jpegXL.hasCompiledPixelCodec());
#else
	assert(!jpegXLLossless.hasCompiledPixelCodec());
	assert(!jpegXLJPEGRecompression.hasCompiledPixelCodec());
	assert(!jpegXL.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_RLE
	assert(rle.hasCompiledPixelCodec());
#else
	assert(!rle.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_ZLIB
	assert(deflated.canDecodeDataset());
#else
	assert(!deflated.canDecodeDataset());
#endif

#if DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN
	assert(explicitBig.canDecodeDataset());
#else
	assert(!explicitBig.canDecodeDataset());
#endif

#if DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH
	assert(jpegBaseline.canPassThroughPixelData());
#else
	assert(!jpegBaseline.canPassThroughPixelData());
#endif

	dicom::Server server;
	assert(serverAccepts(server, dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
	assert(serverAccepts(server, dicom::EXPL_VR_LE_TRANSFER_SYNTAX));
	assert(serverAccepts(server, dicom::EXPL_VR_BE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN));
	assert(serverAccepts(server, dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(serverAccepts(server, dicom::RLE_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_RLE) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_BASELINE_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_LOSSLESS_NON_HIERARCHICAL) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_LS_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGLS) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGLS) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG2000_LOSSLESS_ONLY) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG2000) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG2000) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG2000) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_HTJ2K) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_HTJ2K) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::HTJ2K_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_HTJ2K) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_XL_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGXL) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_XL_JPEG_RECOMPRESSION_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGXL) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_XL_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGXL) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));

	dicom::PresentationContexts contexts;
	contexts.Add(dicom::CT_IMAGE_STORAGE_SOP_CLASS);
	assert(hasTransferSyntax(contexts, dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::EXPL_VR_LE_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::EXPL_VR_BE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN));
	assert(hasTransferSyntax(contexts, dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(hasTransferSyntax(contexts, dicom::RLE_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_RLE) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_BASELINE_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_LOSSLESS_NON_HIERARCHICAL) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_LS_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGLS) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGLS) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG2000_LOSSLESS_ONLY) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG2000) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG2000) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG2000) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_HTJ2K) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_HTJ2K) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::HTJ2K_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_HTJ2K) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_XL_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGXL) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_XL_JPEG_RECOMPRESSION_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGXL) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_XL_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEGXL) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));

	return 0;
}
