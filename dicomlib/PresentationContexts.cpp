#include "PresentationContexts.hpp"
#include "dicomlib/Config.hpp"
#include "UIDs.hpp"

namespace dicom
{
	void PresentationContexts::Add(const UID& uid)
	{
		primitive::AbstractSyntax as(uid);
		std::vector<primitive::TransferSyntax> transfer_syntaxes;
		transfer_syntaxes.push_back(primitive::TransferSyntax(EXPL_VR_LE_TRANSFER_SYNTAX));
		transfer_syntaxes.push_back(primitive::TransferSyntax(IMPL_VR_LE_TRANSFER_SYNTAX));
#if DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN
		transfer_syntaxes.push_back(primitive::TransferSyntax(EXPL_VR_BE_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_WITH_ZLIB
		transfer_syntaxes.push_back(primitive::TransferSyntax(DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_WITH_RLE
		transfer_syntaxes.push_back(primitive::TransferSyntax(RLE_LOSSLESS_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_WITH_JPEG
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG_BASELINE_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_WITH_JPEG2000
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG2000_LOSSLESS_ONLY));
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG2000));
#endif
#if DICOMLIB_WITH_HTJ2K
		transfer_syntaxes.push_back(primitive::TransferSyntax(HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX));
		transfer_syntaxes.push_back(primitive::TransferSyntax(HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX));
		transfer_syntaxes.push_back(primitive::TransferSyntax(HTJ2K_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_WITH_JPEGLS
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG_LS_LOSSLESS_TRANSFER_SYNTAX));
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_WITH_JPEGXL
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG_XL_LOSSLESS_TRANSFER_SYNTAX));
		transfer_syntaxes.push_back(primitive::TransferSyntax(JPEG_XL_TRANSFER_SYNTAX));
#endif
#if DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH
		std::vector<UID> encapsulated = GetEncapsulatedTransferSyntaxUIDs();
		for(size_t i=0;i<encapsulated.size();++i)
		{
#if DICOMLIB_WITH_RLE
			const bool skipRLE = encapsulated[i] == RLE_LOSSLESS_TRANSFER_SYNTAX;
#else
			const bool skipRLE = false;
#endif
#if DICOMLIB_WITH_JPEG
			const bool skipJPEGBaseline = encapsulated[i] == JPEG_BASELINE_TRANSFER_SYNTAX;
#else
			const bool skipJPEGBaseline = false;
#endif
#if DICOMLIB_WITH_JPEG2000
			const bool skipJPEG2000 = encapsulated[i] == JPEG2000_LOSSLESS_ONLY || encapsulated[i] == JPEG2000;
#else
			const bool skipJPEG2000 = false;
#endif
#if DICOMLIB_WITH_HTJ2K
			const bool skipHTJ2K =
				encapsulated[i] == HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX ||
				encapsulated[i] == HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX ||
				encapsulated[i] == HTJ2K_TRANSFER_SYNTAX;
#else
			const bool skipHTJ2K = false;
#endif
#if DICOMLIB_WITH_JPEGLS
			const bool skipJPEGLS =
				encapsulated[i] == JPEG_LS_LOSSLESS_TRANSFER_SYNTAX ||
				encapsulated[i] == JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX;
#else
			const bool skipJPEGLS = false;
#endif
#if DICOMLIB_WITH_JPEGXL
			const bool skipJPEGXL =
				encapsulated[i] == JPEG_XL_LOSSLESS_TRANSFER_SYNTAX ||
				encapsulated[i] == JPEG_XL_TRANSFER_SYNTAX;
#else
			const bool skipJPEGXL = false;
#endif
			if(!skipRLE && !skipJPEGBaseline && !skipJPEG2000 && !skipHTJ2K && !skipJPEGLS && !skipJPEGXL)
				transfer_syntaxes.push_back(primitive::TransferSyntax(encapsulated[i]));
		}
#endif
		primitive::PresentationContext p(as,transfer_syntaxes,IDGenerator_());
		push_back(p);
	}

	void PresentationContexts::Add(const UID& uid, const TS ts)
	{
		std::vector<primitive::TransferSyntax> transfer_syntaxes;
		transfer_syntaxes.push_back(primitive::TransferSyntax(ts.getUID()));

		primitive::AbstractSyntax as(uid);
		primitive::PresentationContext p(as,transfer_syntaxes,IDGenerator_());
		push_back(p);
	}
	void PresentationContexts::Add(const std::vector<UID>& Abstract_Syntax_UIDs, const std::vector<UID>& Transfer_Syntaxs)
	{
		//convert dicom::TS to primitive::TransferSyntax
		std::vector<primitive::TransferSyntax> transfer_syntaxes;
		for(size_t i=0;i<Transfer_Syntaxs.size();i++)
			transfer_syntaxes.push_back(primitive::TransferSyntax(Transfer_Syntaxs[i]));

		for (size_t i=0;i<Abstract_Syntax_UIDs.size();i++)
		{
			primitive::AbstractSyntax as(Abstract_Syntax_UIDs[i]);
			primitive::PresentationContext p(as,transfer_syntaxes,IDGenerator_());
			push_back(p);
		}
	}
}//namespace dicom
