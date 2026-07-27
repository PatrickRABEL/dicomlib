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
#if DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH
		std::vector<UID> encapsulated = GetEncapsulatedTransferSyntaxUIDs();
		for(size_t i=0;i<encapsulated.size();++i)
			transfer_syntaxes.push_back(primitive::TransferSyntax(encapsulated[i]));
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
