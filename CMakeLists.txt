cmake_minimum_required(VERSION 3.16)

project(dicomlib VERSION 0.9.5 LANGUAGES CXX)

option(DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN "Accept Explicit VR Big Endian transfer syntax for native dataset encode/decode" ON)
option(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH "Accept encapsulated transfer syntaxes only for fragment pass-through without pixel decoding" OFF)
option(DICOMLIB_WITH_ZLIB "Enable Deflated Explicit VR Little Endian codec support" OFF)
option(DICOMLIB_WITH_RLE "Enable RLE Lossless pixel codec support" OFF)
option(DICOMLIB_WITH_JPEG "Enable JPEG pixel codec support" OFF)
option(DICOMLIB_WITH_JPEGLS "Enable JPEG-LS pixel codec support" OFF)
option(DICOMLIB_WITH_JPEG2000 "Enable JPEG 2000 pixel codec support" OFF)
option(DICOMLIB_WITH_HTJ2K "Enable High-Throughput JPEG 2000 pixel codec support" OFF)
option(DICOMLIB_WITH_JPEGXL "Enable JPEG XL pixel codec support" OFF)
option(DICOMLIB_WITH_FFMPEG "Enable MPEG/video pixel codec support" OFF)
option(DICOMLIB_WITH_GDCM "Enable GDCM-backed DICOM pixel codec support" OFF)
option(DICOMLIB_PREPARE_EXTERNAL_CODECS "Require all external libraries needed for future pixel-compressed transfer syntax support" OFF)

set(DICOMLIB_REQUIRE_GDCM OFF)
set(DICOMLIB_REQUIRE_CHARLS OFF)
set(DICOMLIB_REQUIRE_OPENJPEG OFF)
set(DICOMLIB_REQUIRE_OPENJPH OFF)
set(DICOMLIB_REQUIRE_JPEGXL OFF)
set(DICOMLIB_REQUIRE_FFMPEG_AVCODEC OFF)
set(DICOMLIB_REQUIRE_FFMPEG_AVFORMAT OFF)
set(DICOMLIB_REQUIRE_FFMPEG_AVUTIL OFF)
set(DICOMLIB_REQUIRE_FFMPEG_SWSCALE OFF)

if(DICOMLIB_WITH_GDCM OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    set(DICOMLIB_REQUIRE_GDCM ON)
endif()
if(DICOMLIB_WITH_JPEGLS OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    set(DICOMLIB_REQUIRE_CHARLS ON)
endif()
if(DICOMLIB_WITH_JPEG2000 OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    set(DICOMLIB_REQUIRE_OPENJPEG ON)
endif()
if(DICOMLIB_WITH_HTJ2K OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    set(DICOMLIB_REQUIRE_OPENJPH ON)
endif()
if(DICOMLIB_WITH_JPEGXL OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    set(DICOMLIB_REQUIRE_JPEGXL ON)
endif()
if(DICOMLIB_WITH_FFMPEG OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    set(DICOMLIB_REQUIRE_FFMPEG_AVCODEC ON)
    set(DICOMLIB_REQUIRE_FFMPEG_AVFORMAT ON)
    set(DICOMLIB_REQUIRE_FFMPEG_AVUTIL ON)
    set(DICOMLIB_REQUIRE_FFMPEG_SWSCALE ON)
endif()

include(cmake/DicomlibCodecDependencies.cmake)

if(DICOMLIB_WITH_JPEGLS OR DICOMLIB_WITH_JPEG2000 OR DICOMLIB_WITH_HTJ2K OR
   DICOMLIB_WITH_JPEGXL OR DICOMLIB_WITH_FFMPEG OR DICOMLIB_WITH_GDCM)
    message(FATAL_ERROR "Pixel codec options are declared and their dependencies can be detected, but pixel codec integration is not implemented yet. Leave pixel codec options OFF to remain standards-accurate.")
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
    dicomlib/JPEGCodec.cpp
    dicomlib/Ndimse.cpp
    dicomlib/pdata.cpp
    dicomlib/PresentationContexts.cpp
    dicomlib/QueryRetrieve.cpp
    dicomlib/RLECodec.cpp
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

if(DICOMLIB_WITH_ZLIB OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    target_link_libraries(dicom PUBLIC ZLIB::ZLIB)
endif()

if(DICOMLIB_WITH_JPEG OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    target_link_libraries(dicom PUBLIC JPEG::JPEG)
endif()

if(DICOMLIB_REQUIRE_GDCM)
    target_link_libraries(dicom PUBLIC ${GDCM_LIBRARIES})
endif()

if(DICOMLIB_REQUIRE_CHARLS)
    target_link_libraries(dicom PUBLIC PkgConfig::DICOMLIB_CHARLS)
endif()

if(DICOMLIB_REQUIRE_OPENJPEG)
    target_link_libraries(dicom PUBLIC PkgConfig::DICOMLIB_OPENJPEG)
endif()

if(DICOMLIB_REQUIRE_OPENJPH)
    target_link_libraries(dicom PUBLIC PkgConfig::DICOMLIB_OPENJPH)
endif()

if(DICOMLIB_REQUIRE_JPEGXL)
    target_link_libraries(dicom PUBLIC PkgConfig::DICOMLIB_JPEGXL)
endif()

if(DICOMLIB_WITH_FFMPEG OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    target_link_libraries(dicom PUBLIC
        PkgConfig::DICOMLIB_FFMPEG_AVCODEC
        PkgConfig::DICOMLIB_FFMPEG_AVFORMAT
        PkgConfig::DICOMLIB_FFMPEG_AVUTIL
        PkgConfig::DICOMLIB_FFMPEG_SWSCALE
    )
endif()

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

    add_executable(dicom_transfer_syntax_roundtrip_tests
        tests/transfer_syntax_roundtrip.cpp
    )
    target_link_libraries(dicom_transfer_syntax_roundtrip_tests PRIVATE dicom)
    add_test(NAME transfer_syntax_roundtrip COMMAND dicom_transfer_syntax_roundtrip_tests)

    add_executable(dicom_rle_codec_tests
        tests/rle_codec.cpp
    )
    target_link_libraries(dicom_rle_codec_tests PRIVATE dicom)
    add_test(NAME rle_codec COMMAND dicom_rle_codec_tests)
endif()
