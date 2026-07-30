if(NOT DEFINED DICOMLIB_SOURCE_DIR)
    message(FATAL_ERROR "DICOMLIB_SOURCE_DIR is required")
endif()
if(NOT DEFINED DICOMLIB_BINARY_DIR)
    message(FATAL_ERROR "DICOMLIB_BINARY_DIR is required")
endif()
if(NOT DEFINED DICOMLIB_CXX_COMPILER)
    message(FATAL_ERROR "DICOMLIB_CXX_COMPILER is required")
endif()

file(GLOB DICOMLIB_STANDALONE_HEADERS
    "${DICOMLIB_SOURCE_DIR}/dicomlib/*.hpp"
    "${DICOMLIB_SOURCE_DIR}/socket/*.hpp"
)
list(SORT DICOMLIB_STANDALONE_HEADERS)

set(DICOMLIB_HEADER_CHECK_DIR "${DICOMLIB_BINARY_DIR}/header-self-check")
file(MAKE_DIRECTORY "${DICOMLIB_HEADER_CHECK_DIR}")

foreach(header IN LISTS DICOMLIB_STANDALONE_HEADERS)
    file(RELATIVE_PATH relative_header "${DICOMLIB_SOURCE_DIR}" "${header}")
    string(REPLACE "/" "_" test_name "${relative_header}")
    set(test_source "${DICOMLIB_HEADER_CHECK_DIR}/${test_name}.cpp")
    file(WRITE "${test_source}" "#include \"${relative_header}\"\nint main(){return 0;}\n")
    execute_process(
        COMMAND
            "${DICOMLIB_CXX_COMPILER}"
            -std=c++17
            -I "${DICOMLIB_SOURCE_DIR}"
            -I "${DICOMLIB_BINARY_DIR}/generated"
            -fsyntax-only
            "${test_source}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error
    )
    if(NOT compile_result EQUAL 0)
        message(STATUS "${compile_output}")
        message(STATUS "${compile_error}")
        message(FATAL_ERROR "Header does not compile standalone: ${relative_header}")
    endif()
endforeach()

message(STATUS "dicomlib and socket headers compile standalone")
