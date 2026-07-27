cmake_minimum_required(VERSION 3.16)

project(dicomlib VERSION 0.9.5 LANGUAGES CXX)

option(DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN "Accept Explicit VR Big Endian transfer syntax for native dataset encode/decode" ON)
option(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH "Accept encapsulated transfer syntaxes only for byte-exact Pixel Data pass-through" OFF)
option(DICOMLIB_WITH_ZLIB "Enable Deflated Explicit VR Little Endian codec support" OFF)
option(DICOMLIB_WITH_RLE "Enable RLE Lossless pixel codec support" OFF)
option(DICOMLIB_WITH_JPEG "Enable JPEG pixel codec support" OFF)
option(DICOMLIB_WITH_JPEGLS "Enable JPEG-LS pixel codec support" OFF)
option(DICOMLIB_WITH_JPEG2000 "Enable JPEG 2000 pixel codec support" OFF)
option(DICOMLIB_WITH_JPEGXL "Enable JPEG XL pixel codec support" OFF)
option(DICOMLIB_WITH_FFMPEG "Enable MPEG/video pixel codec support" OFF)

if(DICOMLIB_WITH_ZLIB OR DICOMLIB_WITH_RLE OR DICOMLIB_WITH_JPEG OR DICOMLIB_WITH_JPEGLS OR
   DICOMLIB_WITH_JPEG2000 OR DICOMLIB_WITH_JPEGXL OR DICOMLIB_WITH_FFMPEG)
    message(FATAL_ERROR "Codec options are declared but not implemented yet. Leave codec options OFF to remain standards-accurate.")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "dicomlib supports Linux and macOS only")
endif()

set(DICOMLIB_SUPPORTED_PROCESSORS
    x86
    i386
    i686
    x86_64
    amd64
    arm
    armv7
    armv7l
    aarch64
    arm64
)

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" DICOMLIB_PROCESSOR)
if(NOT DICOMLIB_PROCESSOR IN_LIST DICOMLIB_SUPPORTED_PROCESSORS)
    message(FATAL_ERROR "Unsupported processor '${CMAKE_SYSTEM_PROCESSOR}'. Supported: x86, x86_64, ARM 32-bit, ARM 64-bit")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/dicomlib/Config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/dicomlib/Config.hpp"
)

add_library(dicom STATIC
    dicomlib/aaac.cpp
    dicomlib/aarj.cpp
    dicomlib/aarq.cpp
    dicomlib/AssociationRejection.cpp
    dicomlib/Buffer.cpp
    dicomlib/Cdimse.cpp
    dicomlib/ClientConnection.cpp
    dicomlib/CommandSets.cpp
    dicomlib/DataDictionary.cpp
    dicomlib/Decoder.cpp
    dicomlib/Dumper.cpp
    dicomlib/Encoder.cpp
    dicomlib/Exceptions.cpp
    dicomlib/File.cpp
    dicomlib/FileMetaInformation.cpp
    dicomlib/GroupLength.cpp
    dicomlib/Ndimse.cpp
    dicomlib/pdata.cpp
    dicomlib/PresentationContexts.cpp
    dicomlib/QueryRetrieve.cpp
    dicomlib/Server.cpp
    dicomlib/ServiceBase.cpp
    dicomlib/Tag.cpp
    dicomlib/ThreadSpecificServer.cpp
    dicomlib/TransferSyntax.cpp
    dicomlib/UID.cpp
    dicomlib/UIDs.cpp
    dicomlib/Utility.cpp
    dicomlib/ValueToStream.cpp
    dicomlib/Version.cpp
    dicomlib/ViewCodeSequence.cpp
)

target_include_directories(dicom
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_features(dicom PUBLIC cxx_std_17)
set_target_properties(dicom PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    OUTPUT_NAME dicom
)

target_compile_options(dicom PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

find_package(Threads REQUIRED)
target_link_libraries(dicom PUBLIC Threads::Threads)

include(CTest)

if(BUILD_TESTING)
    add_executable(dicom_cdimse_tests
        tests/cdimse_commandsets.cpp
    )
    target_link_libraries(dicom_cdimse_tests PRIVATE dicom)
    add_test(NAME cdimse_commandsets COMMAND dicom_cdimse_tests)

    add_executable(dicom_transfer_syntax_tests
        tests/transfer_syntax_support.cpp
    )
    target_link_libraries(dicom_transfer_syntax_tests PRIVATE dicom)
    add_test(NAME transfer_syntax_support COMMAND dicom_transfer_syntax_tests)
endif()
