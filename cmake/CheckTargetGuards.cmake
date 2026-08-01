if(NOT DEFINED DICOMLIB_SOURCE_DIR)
    message(FATAL_ERROR "DICOMLIB_SOURCE_DIR is required")
endif()
if(NOT DEFINED DICOMLIB_BINARY_DIR)
    message(FATAL_ERROR "DICOMLIB_BINARY_DIR is required")
endif()

set(DICOMLIB_UNSUPPORTED_PROCESSOR_CHECK_DIR
    "${DICOMLIB_BINARY_DIR}/target-guard-check/unsupported-processor")
file(REMOVE_RECURSE "${DICOMLIB_UNSUPPORTED_PROCESSOR_CHECK_DIR}")
file(MAKE_DIRECTORY "${DICOMLIB_UNSUPPORTED_PROCESSOR_CHECK_DIR}")
set(DICOMLIB_UNSUPPORTED_PROCESSOR_TOOLCHAIN
    "${DICOMLIB_UNSUPPORTED_PROCESSOR_CHECK_DIR}/unsupported-processor-toolchain.cmake")
file(WRITE "${DICOMLIB_UNSUPPORTED_PROCESSOR_TOOLCHAIN}"
    "set(CMAKE_SYSTEM_NAME \"${CMAKE_HOST_SYSTEM_NAME}\")\n"
    "set(CMAKE_SYSTEM_PROCESSOR mips64)\n")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -S "${DICOMLIB_SOURCE_DIR}"
        -B "${DICOMLIB_UNSUPPORTED_PROCESSOR_CHECK_DIR}"
        -D CMAKE_TOOLCHAIN_FILE=${DICOMLIB_UNSUPPORTED_PROCESSOR_TOOLCHAIN}
        -D BUILD_TESTING=OFF
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)

if(configure_result EQUAL 0)
    message(FATAL_ERROR "Unsupported processor mips64 configured successfully")
endif()

set(configure_text "${configure_output}\n${configure_error}")
if(NOT configure_text MATCHES "Unsupported processor")
    message(STATUS "${configure_text}")
    message(FATAL_ERROR "Unsupported processor configure failed for an unexpected reason")
endif()

message(STATUS "unsupported processor target guard remains blocked")
