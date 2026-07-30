cmake_minimum_required(VERSION 3.16)

project(dicomlib VERSION 0.9.5 LANGUAGES CXX)

option(DICOMLIB_ENABLE_EXPLICIT_VR_BIG_ENDIAN "Accept Explicit VR Big Endian transfer syntax for native dataset encode/decode" ON)
option(DICOMLIB_ENABLE_ENCAPSULATED_PASSTHROUGH "Accept encapsulated transfer syntaxes only for fragment pass-through without pixel decoding" OFF)
option(DICOMLIB_WITH_ZLIB "Enable Deflated Explicit VR Little Endian codec support" OFF)
option(DICOMLIB_WITH_RLE "Enable RLE Lossless pixel codec support" OFF)
option(DICOMLIB_WITH_JPEG "Enable JPEG pixel codec support" OFF)
option(DICOMLIB_WITH_JPEGLS "Enable JPEG-LS pixel codec support" OFF)
set(DICOMLIB_JPEGLS_NEAR_LOSSLESS "1" CACHE STRING "NEAR value used when encoding JPEG-LS Near-Lossless transfer syntax")
option(DICOMLIB_WITH_JPEG2000 "Enable JPEG 2000 pixel codec support" OFF)
option(DICOMLIB_WITH_HTJ2K "Enable High-Throughput JPEG 2000 pixel codec support" OFF)
set(DICOMLIB_HTJ2K_QFACTOR "90" CACHE STRING "Quality factor used when encoding HTJ2K general lossy transfer syntax")
option(DICOMLIB_WITH_JPEGXL "Enable JPEG XL pixel codec support" OFF)
set(DICOMLIB_JPEGXL_DISTANCE "1.0" CACHE STRING "Distance used when encoding JPEG XL general lossy transfer syntax")
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

if(DICOMLIB_WITH_JPEGLS)
    if(DICOMLIB_JPEGLS_NEAR_LOSSLESS LESS 1)
        message(FATAL_ERROR "DICOMLIB_JPEGLS_NEAR_LOSSLESS must be greater than or equal to 1")
    endif()
endif()

if(DICOMLIB_WITH_JPEGXL)
    if(DICOMLIB_JPEGXL_DISTANCE LESS_EQUAL 0 OR DICOMLIB_JPEGXL_DISTANCE GREATER 25)
        message(FATAL_ERROR "DICOMLIB_JPEGXL_DISTANCE must be greater than 0 and less than or equal to 25")
    endif()
endif()

if(DICOMLIB_WITH_HTJ2K)
    if(DICOMLIB_HTJ2K_QFACTOR LESS 1 OR DICOMLIB_HTJ2K_QFACTOR GREATER 100)
        message(FATAL_ERROR "DICOMLIB_HTJ2K_QFACTOR must be between 1 and 100")
    endif()
endif()

if(DICOMLIB_WITH_FFMPEG)
    message(FATAL_ERROR "FFmpeg pixel codec dependencies can be detected, but MPEG/video pixel codec integration is not implemented yet. Leave DICOMLIB_WITH_FFMPEG OFF to remain standards-accurate.")
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
    dicomlib/DeflatedImageFrameCodec.cpp
    dicomlib/Decoder.cpp
    dicomlib/Dumper.cpp
    dicomlib/EncapsulatedUncompressedCodec.cpp
    dicomlib/Encoder.cpp
    dicomlib/Exceptions.cpp
    dicomlib/File.cpp
    dicomlib/FileMetaInformation.cpp
    dicomlib/GDCMJPEGCodec.cpp
    dicomlib/GroupLength.cpp
    dicomlib/HTJ2KCodec.cpp
    dicomlib/JPIPReferencedCodec.cpp
    dicomlib/JPEG2000Codec.cpp
    dicomlib/JPEGCodec.cpp
    dicomlib/JPEGLSCodec.cpp
    dicomlib/JPEGXLCodec.cpp
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
    target_include_directories(dicom PUBLIC ${GDCM_INCLUDE_DIRS})
    target_link_libraries(dicom PUBLIC
        gdcmMSFF
        gdcmDSED
        gdcmDICT
        gdcmIOD
        gdcmCommon
        gdcmjpeg8
        gdcmjpeg12
        gdcmjpeg16
    )
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
    add_test(
        NAME standalone_headers
        COMMAND
            ${CMAKE_COMMAND}
            -D DICOMLIB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -D DICOMLIB_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D DICOMLIB_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckStandaloneHeaders.cmake
    )

    add_test(
        NAME core_cleanup
        COMMAND
            ${CMAKE_COMMAND}
            -D DICOMLIB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckCoreCleanup.cmake
    )

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
