include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)

function(dicomlib_find_pkg_config_dependency option_name target_name module_name display_name)
    if(NOT ${option_name})
        set(${option_name}_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR "${display_name} requires pkg-config for dependency discovery")
    endif()

    pkg_check_modules(${target_name} REQUIRED IMPORTED_TARGET ${module_name})
    set(${option_name}_FOUND TRUE PARENT_SCOPE)
endfunction()

if(DICOMLIB_WITH_ZLIB OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    find_package(ZLIB REQUIRED)
endif()

if(DICOMLIB_WITH_JPEG OR DICOMLIB_PREPARE_EXTERNAL_CODECS)
    find_package(JPEG REQUIRED)
endif()

if(DICOMLIB_REQUIRE_GDCM)
    message(STATUS "GDCM is required for DICOM-specific legacy JPEG/RLE/JPEG-LS/JPEG 2000 codec preparation")
    find_package(GDCM REQUIRED)
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
