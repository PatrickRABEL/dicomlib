if(NOT DEFINED DICOMLIB_SOURCE_DIR)
    message(FATAL_ERROR "DICOMLIB_SOURCE_DIR is required")
endif()
if(NOT DEFINED DICOMLIB_BINARY_DIR)
    message(FATAL_ERROR "DICOMLIB_BINARY_DIR is required")
endif()

set(DICOMLIB_UNSUPPORTED_CODEC_CHECK_DIR
    "${DICOMLIB_BINARY_DIR}/unsupported-codec-option-check/ffmpeg")
file(REMOVE_RECURSE "${DICOMLIB_UNSUPPORTED_CODEC_CHECK_DIR}")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${DICOMLIB_SOURCE_DIR}"
        -B "${DICOMLIB_UNSUPPORTED_CODEC_CHECK_DIR}"
        -D DICOMLIB_WITH_FFMPEG=ON
        -D BUILD_TESTING=OFF
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)

if(configure_result EQUAL 0)
    message(FATAL_ERROR "DICOMLIB_WITH_FFMPEG=ON configured successfully, but MPEG/video codec integration is not implemented")
endif()

set(configure_text "${configure_output}\n${configure_error}")
if(NOT configure_text MATCHES "FFmpeg pixel codec dependencies")
    message(STATUS "${configure_text}")
    message(FATAL_ERROR "DICOMLIB_WITH_FFMPEG=ON failed for an unexpected reason")
endif()

message(STATUS "unsupported FFmpeg codec option remains blocked")

function(check_invalid_codec_setting name expected_message)
    set(check_dir "${DICOMLIB_BINARY_DIR}/unsupported-codec-option-check/${name}")
    file(REMOVE_RECURSE "${check_dir}")

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            -S "${DICOMLIB_SOURCE_DIR}"
            -B "${check_dir}"
            ${ARGN}
            -D BUILD_TESTING=OFF
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_output
        ERROR_VARIABLE configure_error
    )

    if(configure_result EQUAL 0)
        message(FATAL_ERROR "${name} configured successfully, but it should be rejected")
    endif()

    set(configure_text "${configure_output}\n${configure_error}")
    if(NOT configure_text MATCHES "${expected_message}")
        message(STATUS "${configure_text}")
        message(FATAL_ERROR "${name} failed for an unexpected reason")
    endif()

    message(STATUS "${name} remains blocked")
endfunction()

check_invalid_codec_setting(
    "jpegls-near-lossless-zero"
    "DICOMLIB_JPEGLS_NEAR_LOSSLESS"
    -D DICOMLIB_WITH_JPEGLS=ON
    -D DICOMLIB_JPEGLS_NEAR_LOSSLESS=0)
check_invalid_codec_setting(
    "jpegxl-distance-zero"
    "DICOMLIB_JPEGXL_DISTANCE"
    -D DICOMLIB_WITH_JPEGXL=ON
    -D DICOMLIB_JPEGXL_DISTANCE=0)
check_invalid_codec_setting(
    "jpegxl-distance-too-high"
    "DICOMLIB_JPEGXL_DISTANCE"
    -D DICOMLIB_WITH_JPEGXL=ON
    -D DICOMLIB_JPEGXL_DISTANCE=26)
check_invalid_codec_setting(
    "htj2k-qfactor-zero"
    "DICOMLIB_HTJ2K_QFACTOR"
    -D DICOMLIB_WITH_HTJ2K=ON
    -D DICOMLIB_HTJ2K_QFACTOR=0)
check_invalid_codec_setting(
    "htj2k-qfactor-too-high"
    "DICOMLIB_HTJ2K_QFACTOR"
    -D DICOMLIB_WITH_HTJ2K=ON
    -D DICOMLIB_HTJ2K_QFACTOR=101)
