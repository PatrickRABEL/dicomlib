#ifndef IMPLEMENTATION_UID_HPP_INCLUDE_GUARD_358394857
#define IMPLEMENTATION_UID_HPP_INCLUDE_GUARD_358394857
#include <string>

namespace dicom
{
	//!Default Implementation Class UID announced by dicomlib.
	/*!
		Applications embedding the library can override this value on Server when
		they need to announce their own implementation identity.
	*/
	const std::string ImplementationClassUID = "1.2.826.0.1.3680043.10.1778";
	//!Default Implementation Version Name, limited to PS3.7's 16-character field.
	const std::string ImplementationVersionName = "DICOMLIB2026";
}//namespace dicom



#endif //IMPLEMENTATION_UID_HPP_INCLUDE_GUARD_358394857
