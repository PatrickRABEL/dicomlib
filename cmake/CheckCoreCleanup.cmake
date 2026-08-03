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

file(GLOB_RECURSE DICOMLIB_LEGACY_VISUAL_STUDIO
    "${DICOMLIB_SOURCE_DIR}/*.sln"
    "${DICOMLIB_SOURCE_DIR}/*.suo"
    "${DICOMLIB_SOURCE_DIR}/*.vcproj"
    "${DICOMLIB_SOURCE_DIR}/*.vcxproj"
    "${DICOMLIB_SOURCE_DIR}/*.vcxproj.filters"
    "${DICOMLIB_SOURCE_DIR}/*/*.sln"
    "${DICOMLIB_SOURCE_DIR}/*/*.suo"
    "${DICOMLIB_SOURCE_DIR}/*/*.vcproj"
    "${DICOMLIB_SOURCE_DIR}/*/*.vcxproj"
    "${DICOMLIB_SOURCE_DIR}/*/*.vcxproj.filters"
)
list(FILTER DICOMLIB_LEGACY_VISUAL_STUDIO EXCLUDE REGEX "/build[^/]*/")
if(DICOMLIB_LEGACY_VISUAL_STUDIO)
    list(JOIN DICOMLIB_LEGACY_VISUAL_STUDIO "\n  - " DICOMLIB_LEGACY_VISUAL_STUDIO_TEXT)
    message(FATAL_ERROR "Legacy Visual Studio build files remain in core sources:\n  - ${DICOMLIB_LEGACY_VISUAL_STUDIO_TEXT}")
endif()

file(GLOB DICOMLIB_LEGACY_UTILITY
    "${DICOMLIB_SOURCE_DIR}/utility/*"
)
if(DICOMLIB_LEGACY_UTILITY)
    list(JOIN DICOMLIB_LEGACY_UTILITY "\n  - " DICOMLIB_LEGACY_UTILITY_TEXT)
    message(FATAL_ERROR "Legacy utility sources remain outside the maintained CMake library:\n  - ${DICOMLIB_LEGACY_UTILITY_TEXT}")
endif()

file(GLOB_RECURSE DICOMLIB_CORE_PLATFORM_SOURCES
    "${DICOMLIB_SOURCE_DIR}/dicomlib/*.cpp"
    "${DICOMLIB_SOURCE_DIR}/dicomlib/*.hpp"
    "${DICOMLIB_SOURCE_DIR}/socket/*.cpp"
    "${DICOMLIB_SOURCE_DIR}/socket/*.hpp"
)
foreach(source IN LISTS DICOMLIB_CORE_PLATFORM_SOURCES)
    file(READ "${source}" source_text)
    if(source_text MATCHES "(_WIN32|WinSock|WSAStartup|WSACleanup|windows\\.h|winsock)")
        file(RELATIVE_PATH relative_source "${DICOMLIB_SOURCE_DIR}" "${source}")
        message(FATAL_ERROR "Windows-specific code remains in maintained core sources: ${relative_source}")
    endif()
endforeach()

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

message(STATUS "core sources have no Boost includes or legacy build files")
