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
	dicom::TS encapsulatedUncompressed(dicom::ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX);
	dicom::TS deflated(dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX);
	dicom::TS deflatedImageFrame(dicom::DEFLATED_IMAGE_FRAME_COMPRESSION_TRANSFER_SYNTAX);
	dicom::TS jpegBaseline(dicom::JPEG_BASELINE_TRANSFER_SYNTAX);
	dicom::TS jpegExtendedProcess24(dicom::JPEG_EXTENDED_PROCESS_2_4_TRANSFER_SYNTAX);
	dicom::TS jpegExtendedProcess35(dicom::JPEG_EXTENDED_PROCESS_3_5_TRANSFER_SYNTAX);
	dicom::TS jpegSpectralSelectionProcess68(dicom::JPEG_SPECTRAL_SELECTION_PROCESS_6_8_TRANSFER_SYNTAX);
	dicom::TS jpegFullProgressionProcess1012(dicom::JPEG_FULL_PROGRESSION_PROCESS_10_12_TRANSFER_SYNTAX);
	dicom::TS jpegLosslessProcess14(dicom::JPEG_LOSSLESS_PROCESS_14_TRANSFER_SYNTAX);
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
	dicom::TS jpipReferenced(dicom::JPIP_REFERENCED_TRANSFER_SYNTAX);
	dicom::TS jpipReferencedDeflate(dicom::JPIP_REFERENCED_DEFLATE_TRANSFER_SYNTAX);
	dicom::TS jpipHTJ2KReferenced(dicom::JPIP_HTJ2K_REFERENCED_TRANSFER_SYNTAX);
	dicom::TS jpipHTJ2KReferencedDeflate(dicom::JPIP_HTJ2K_REFERENCED_DEFLATE_TRANSFER_SYNTAX);
	dicom::TS rle(dicom::RLE_LOSSLESS_TRANSFER_SYNTAX);

	assert(implicitLittle.canDecodeDataset());
	assert(explicitLittle.canDecodeDataset());
	assert(encapsulatedUncompressed.isEncapsulated());
	assert(encapsulatedUncompressed.hasCompiledPixelCodec());
	assert(deflatedImageFrame.isEncapsulated());
	assert(!jpegBaseline.canDecodeDataset());
	assert(jpegBaseline.isEncapsulated());
	assert(jpegExtendedProcess24.isEncapsulated());
	assert(jpegExtendedProcess35.isEncapsulated());
	assert(jpegSpectralSelectionProcess68.isEncapsulated());
	assert(jpegFullProgressionProcess1012.isEncapsulated());
	assert(jpegLosslessProcess14.isEncapsulated());
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
	assert(jpipReferenced.isJPIPReferenced());
	assert(jpipReferencedDeflate.isJPIPReferenced());
	assert(jpipHTJ2KReferenced.isJPIPReferenced());
	assert(jpipHTJ2KReferencedDeflate.isJPIPReferenced());
	assert(jpipReferenced.canDecodeDataset());
	assert(jpipHTJ2KReferenced.canDecodeDataset());
	assert(!jpipReferenced.hasCompiledPixelCodec());
	assert(!jpipReferencedDeflate.hasCompiledPixelCodec());
	assert(!jpipHTJ2KReferenced.hasCompiledPixelCodec());
	assert(!jpipHTJ2KReferencedDeflate.hasCompiledPixelCodec());
	assert(!jpipReferenced.canPassThroughPixelData());
	assert(!jpipHTJ2KReferenced.canPassThroughPixelData());

#if DICOMLIB_WITH_JPEG
	assert(jpegBaseline.hasCompiledPixelCodec());
#else
	assert(!jpegBaseline.hasCompiledPixelCodec());
#endif

#if DICOMLIB_WITH_GDCM
	assert(jpegExtendedProcess24.hasCompiledPixelCodec());
	assert(jpegExtendedProcess35.hasCompiledPixelCodec());
	assert(jpegSpectralSelectionProcess68.hasCompiledPixelCodec());
	assert(jpegFullProgressionProcess1012.hasCompiledPixelCodec());
	assert(jpegLosslessProcess14.hasCompiledPixelCodec());
	assert(jpegLosslessProcess14SV1.hasCompiledPixelCodec());
#else
	assert(!jpegExtendedProcess24.hasCompiledPixelCodec());
	assert(!jpegExtendedProcess35.hasCompiledPixelCodec());
	assert(!jpegSpectralSelectionProcess68.hasCompiledPixelCodec());
	assert(!jpegFullProgressionProcess1012.hasCompiledPixelCodec());
	assert(!jpegLosslessProcess14.hasCompiledPixelCodec());
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
	assert(deflatedImageFrame.hasCompiledPixelCodec());
	assert(jpipReferencedDeflate.canDecodeDataset());
	assert(jpipHTJ2KReferencedDeflate.canDecodeDataset());
#else
	assert(!deflated.canDecodeDataset());
	assert(!deflatedImageFrame.hasCompiledPixelCodec());
	assert(!jpipReferencedDeflate.canDecodeDataset());
	assert(!jpipHTJ2KReferencedDeflate.canDecodeDataset());
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
	assert(serverAccepts(server, dicom::ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX));
	assert(serverAccepts(server, dicom::JPIP_REFERENCED_TRANSFER_SYNTAX));
	assert(serverAccepts(server, dicom::JPIP_HTJ2K_REFERENCED_TRANSFER_SYNTAX));
	assert(serverAccepts(server, dicom::JPIP_REFERENCED_DEFLATE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(serverAccepts(server, dicom::JPIP_HTJ2K_REFERENCED_DEFLATE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(serverAccepts(server, dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(serverAccepts(server, dicom::DEFLATED_IMAGE_FRAME_COMPRESSION_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_ZLIB) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::RLE_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_RLE) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_BASELINE_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_EXTENDED_PROCESS_2_4_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_EXTENDED_PROCESS_3_5_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_SPECTRAL_SELECTION_PROCESS_6_8_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_FULL_PROGRESSION_PROCESS_10_12_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(serverAccepts(server, dicom::JPEG_LOSSLESS_PROCESS_14_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
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
	assert(hasTransferSyntax(contexts, dicom::ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::JPIP_REFERENCED_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::JPIP_HTJ2K_REFERENCED_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::JPIP_REFERENCED_DEFLATE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(hasTransferSyntax(contexts, dicom::JPIP_HTJ2K_REFERENCED_DEFLATE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(hasTransferSyntax(contexts, dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_WITH_ZLIB));
	assert(hasTransferSyntax(contexts, dicom::DEFLATED_IMAGE_FRAME_COMPRESSION_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_ZLIB) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::RLE_LOSSLESS_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_RLE) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_BASELINE_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_JPEG) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_EXTENDED_PROCESS_2_4_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_EXTENDED_PROCESS_3_5_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_SPECTRAL_SELECTION_PROCESS_6_8_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_FULL_PROGRESSION_PROCESS_10_12_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
	assert(hasTransferSyntax(contexts, dicom::JPEG_LOSSLESS_PROCESS_14_TRANSFER_SYNTAX) ==
		(static_cast<bool>(DICOMLIB_WITH_GDCM) || static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH)));
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
