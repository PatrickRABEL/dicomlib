include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)

set(DICOMLIB_MISSING_CODEC_DEPENDENCIES "")

macro(dicomlib_note_missing_dependency display_name)
    list(APPEND DICOMLIB_MISSING_CODEC_DEPENDENCIES "${display_name}")
endmacro()

macro(dicomlib_find_pkg_config_dependency option_name target_name module_name display_name)
    if(NOT ${option_name})
        set(${option_name}_FOUND FALSE)
    elseif(NOT PkgConfig_FOUND)
        dicomlib_note_missing_dependency("${display_name}: pkg-config")
        set(${option_name}_FOUND FALSE)
    else()
        pkg_check_modules(${target_name} QUIET IMPORTED_TARGET ${module_name})
        if(${target_name}_FOUND)
            set(${option_name}_FOUND TRUE)
        else()
            dicomlib_note_missing_dependency("${display_name}: ${module_name}")
            set(${option_name}_FOUND FALSE)
        endif()
    endif()
endmacro()

if(DICOMLIB_WITH_ZLIB OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    find_package(ZLIB QUIET)
    if(NOT ZLIB_FOUND)
        dicomlib_note_missing_dependency("Deflated Explicit VR Little Endian support: zlib")
    endif()
endif()

if(DICOMLIB_WITH_JPEG OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    find_package(JPEG QUIET)
    if(NOT JPEG_FOUND)
        dicomlib_note_missing_dependency("Legacy JPEG support: libjpeg or libjpeg-turbo")
    endif()
endif()

if(DICOMLIB_REQUIRE_GDCM)
    message(STATUS "GDCM is required for DICOM-specific legacy JPEG/RLE/JPEG-LS/JPEG 2000 codec preparation")
    find_package(GDCM QUIET)
    if(NOT GDCM_FOUND)
        dicomlib_note_missing_dependency("DICOM pixel codec backend: GDCM")
    endif()
endif()

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_CHARLS
    DICOMLIB_CHARLS
    charls
    "JPEG-LS support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_OPENJPEG
    DICOMLIB_OPENJPEG
    libopenjp2
    "JPEG 2000 support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_OPENJPH
    DICOMLIB_OPENJPH
    openjph
    "High-Throughput JPEG 2000 support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_JPEGXL
    DICOMLIB_JPEGXL
    libjxl
    "JPEG XL support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_FFMPEG_AVCODEC
    DICOMLIB_FFMPEG_AVCODEC
    libavcodec
    "MPEG/video support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_FFMPEG_AVFORMAT
    DICOMLIB_FFMPEG_AVFORMAT
    libavformat
    "MPEG/video support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_FFMPEG_AVUTIL
    DICOMLIB_FFMPEG_AVUTIL
    libavutil
    "MPEG/video support"
)

dicomlib_find_pkg_config_dependency(
    DICOMLIB_REQUIRE_FFMPEG_SWSCALE
    DICOMLIB_FFMPEG_SWSCALE
    libswscale
    "MPEG/video support"
)

if(DICOMLIB_MISSING_CODEC_DEPENDENCIES)
    list(JOIN DICOMLIB_MISSING_CODEC_DEPENDENCIES "\n  - " DICOMLIB_MISSING_CODEC_DEPENDENCIES_TEXT)
    message(FATAL_ERROR "Missing external codec dependencies:\n  - ${DICOMLIB_MISSING_CODEC_DEPENDENCIES_TEXT}")
endif()
