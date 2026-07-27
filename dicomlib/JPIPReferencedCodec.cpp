#include "JPIPReferencedCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "UIDs.hpp"

#include <cctype>
#include <string>

namespace dicom
{
	namespace
	{
		std::string TrimCS(std::string value)
		{
			while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
				value.erase(value.end() - 1);
			while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
				value.erase(value.begin());
			return value;
		}

		bool IsAllowedJPIPPhotometricInterpretation(const std::string& value)
		{
			return value == "MONOCHROME1" ||
				value == "MONOCHROME2" ||
				value == "YBR_ICT" ||
				value == "YBR_RCT";
		}
	}

	void ValidateJPIPReferencedDataSet(const DataSet& data, const UID& transferSyntaxUID)
	{
		const bool isJPIP =
			transferSyntaxUID == JPIP_REFERENCED_TRANSFER_SYNTAX ||
			transferSyntaxUID == JPIP_REFERENCED_DEFLATE_TRANSFER_SYNTAX ||
			transferSyntaxUID == JPIP_HTJ2K_REFERENCED_TRANSFER_SYNTAX ||
			transferSyntaxUID == JPIP_HTJ2K_REFERENCED_DEFLATE_TRANSFER_SYNTAX;
		Enforce(isJPIP, "Transfer Syntax is not JPIP Referenced");

		Enforce(!data.exists(TAG_PIXEL_DATA), "JPIP Referenced Transfer Syntax shall not contain Pixel Data");
		Enforce(!data.exists(TAG_FLOAT_PIXEL_DATA), "JPIP Referenced Transfer Syntax shall not contain Float Pixel Data");
		Enforce(!data.exists(TAG_DOUBLE_FLOAT_PIXEL_DATA), "JPIP Referenced Transfer Syntax shall not contain Double Float Pixel Data");
		Enforce(data.exists(TAG_PIXEL_DATA_PROVIDER_URL), "JPIP Referenced Transfer Syntax requires Pixel Data Provider URL");

		const Value& url = data(TAG_PIXEL_DATA_PROVIDER_URL);
		Enforce(url.vr() == VR_UR, "Pixel Data Provider URL must have VR UR");
		std::string urlValue;
		url >> urlValue;
		Enforce(!urlValue.empty(), "Pixel Data Provider URL must not be empty");

		if(data.exists(TAG_PHOTOMETRIC))
		{
			const Value& photometric = data(TAG_PHOTOMETRIC);
			Enforce(photometric.vr() == VR_CS, "Photometric Interpretation must have VR CS");
			std::string photometricValue;
			photometric >> photometricValue;
			Enforce(
				IsAllowedJPIPPhotometricInterpretation(TrimCS(photometricValue)),
				"JPIP Referenced Photometric Interpretation must be MONOCHROME1, MONOCHROME2, YBR_ICT, or YBR_RCT");
		}
	}
}
