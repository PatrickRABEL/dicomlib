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

	assert(implicitLittle.canDecodeDataset());
	assert(explicitLittle.canDecodeDataset());
	assert(!deflated.canDecodeDataset());
	assert(!jpegBaseline.canDecodeDataset());
	assert(jpegBaseline.isEncapsulated());
	assert(!jpegBaseline.hasCompiledPixelCodec());

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
	assert(!serverAccepts(server, dicom::DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX));
	assert(serverAccepts(server, dicom::JPEG_BASELINE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH));

	dicom::PresentationContexts contexts;
	contexts.Add(dicom::CT_IMAGE_STORAGE_SOP_CLASS);
	assert(hasTransferSyntax(contexts, dicom::IMPL_VR_LE_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::EXPL_VR_LE_TRANSFER_SYNTAX));
	assert(hasTransferSyntax(contexts, dicom::EXPL_VR_BE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN));
	assert(hasTransferSyntax(contexts, dicom::JPEG_BASELINE_TRANSFER_SYNTAX) == static_cast<bool>(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH));

	return 0;
}
