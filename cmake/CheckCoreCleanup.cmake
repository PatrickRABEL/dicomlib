if(NOT DEFINED DICOMLIB_SOURCE_DIR)
    message(FATAL_ERROR "DICOMLIB_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE DICOMLIB_LEGACY_SCONS
    "${DICOMLIB_SOURCE_DIR}/SConstruct"
    "${DICOMLIB_SOURCE_DIR}/SConscript"
    "${DICOMLIB_SOURCE_DIR}/*/SConstruct"
    "${DICOMLIB_SOURCE_DIR}/*/SConscript"
    "${DICOMLIB_SOURCE_DIR}/*/*/SConstruct"
    "${DICOMLIB_SOURCE_DIR}/*/*/SConscript"
)
list(FILTER DICOMLIB_LEGACY_SCONS EXCLUDE REGEX "/build[^/]*/")
if(DICOMLIB_LEGACY_SCONS)
    list(JOIN DICOMLIB_LEGACY_SCONS "\n  - " DICOMLIB_LEGACY_SCONS_TEXT)
    message(FATAL_ERROR "Legacy SCons build files remain in core sources:\n  - ${DICOMLIB_LEGACY_SCONS_TEXT}")
endif()

file(GLOB_RECURSE DICOMLIB_CORE_SOURCES
    "${DICOMLIB_SOURCE_DIR}/dicomlib/*.cpp"
    "${DICOMLIB_SOURCE_DIR}/dicomlib/*.hpp"
    "${DICOMLIB_SOURCE_DIR}/socket/*.cpp"
    "${DICOMLIB_SOURCE_DIR}/socket/*.hpp"
)
foreach(source IN LISTS DICOMLIB_CORE_SOURCES)
    file(READ "${source}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"]boost/")
        file(RELATIVE_PATH relative_source "${DICOMLIB_SOURCE_DIR}" "${source}")
        message(FATAL_ERROR "Boost include remains in core source: ${relative_source}")
    endif()
endforeach()

message(STATUS "core sources have no Boost includes or legacy SCons build files")
