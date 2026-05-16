cmake_minimum_required(VERSION 3.20)

# --------------------------------------------------------------------------- #
# Project metadata
# --------------------------------------------------------------------------- #
project(bencode
    VERSION 0.1.0
    DESCRIPTION "A small, strict Bencode parser, emitter, and CLI in modern C."
    LANGUAGES C
)

# --------------------------------------------------------------------------- #
# Standards & global settings
# --------------------------------------------------------------------------- #
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(BENCODE_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BENCODE_IS_TOP_LEVEL ON)
endif()

# --------------------------------------------------------------------------- #
# Options
# --------------------------------------------------------------------------- #
option(BENCODE_BUILD_TESTS    "Build unit tests"            ${BENCODE_IS_TOP_LEVEL})
option(BENCODE_BUILD_EXAMPLES "Build example programs"      ${BENCODE_IS_TOP_LEVEL})
option(BENCODE_BUILD_CLI      "Build the bencode CLI"       ${BENCODE_IS_TOP_LEVEL})
option(BENCODE_WARNINGS_AS_ERRORS "Treat warnings as errors" ${BENCODE_IS_TOP_LEVEL})
option(BENCODE_ENABLE_ASAN    "Enable AddressSanitizer"     OFF)
option(BENCODE_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(BENCODE_ENABLE_MSAN    "Enable MemorySanitizer (clang only)" OFF)
option(BENCODE_ENABLE_TSAN    "Enable ThreadSanitizer"      OFF)
option(BENCODE_ENABLE_COVERAGE "Enable code-coverage instrumentation" OFF)
option(BENCODE_BUILD_FUZZERS  "Build libFuzzer-based fuzz targets (Clang only)" OFF)
option(BUILD_SHARED_LIBS      "Build libbencode as a shared library" OFF)

# --------------------------------------------------------------------------- #
# Helper modules
# --------------------------------------------------------------------------- #
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# --------------------------------------------------------------------------- #
# Generated version header
# --------------------------------------------------------------------------- #
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/bencode/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/bencode/version.h"
    @ONLY
)

# --------------------------------------------------------------------------- #
# Library target
# --------------------------------------------------------------------------- #
add_library(bencode
    src/parse_sax.c
    src/value.c
    src/parse_dom.c
    src/emit.c
    src/status.c
    src/version.c
)
add_library(bencode::bencode ALIAS bencode)

target_include_directories(bencode
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(bencode PRIVATE src)

set_target_properties(bencode PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME bencode
    POSITION_INDEPENDENT_CODE ON
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(bencode PRIVATE BENCODE_BUILD_SHARED)
    target_compile_definitions(bencode INTERFACE BENCODE_USE_SHARED)
endif()

bencode_apply_warnings(bencode)
bencode_apply_sanitizers(bencode)

# --------------------------------------------------------------------------- #
# CLI executable
# --------------------------------------------------------------------------- #
if(BENCODE_BUILD_CLI)
    add_executable(bencode_cli src/main.c)
    set_target_properties(bencode_cli PROPERTIES OUTPUT_NAME bencode)
    target_link_libraries(bencode_cli PRIVATE bencode::bencode)
    bencode_apply_warnings(bencode_cli)
    bencode_apply_sanitizers(bencode_cli)
endif()

# --------------------------------------------------------------------------- #
# Examples
# --------------------------------------------------------------------------- #
if(BENCODE_BUILD_EXAMPLES)
    add_executable(bencode_decode_example examples/decode_example.c)
    target_link_libraries(bencode_decode_example PRIVATE bencode::bencode)
    bencode_apply_warnings(bencode_decode_example)
    bencode_apply_sanitizers(bencode_decode_example)
endif()

# --------------------------------------------------------------------------- #
# Tests
# --------------------------------------------------------------------------- #
if(BENCODE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BENCODE_BUILD_FUZZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_subdirectory(tests/fuzz)
    else()
        message(WARNING
            "BENCODE_BUILD_FUZZERS=ON ignored: libFuzzer requires Clang "
            "(got ${CMAKE_C_COMPILER_ID}). Re-run with CC=clang to enable.")
        set(BENCODE_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
    endif()
endif()

# --------------------------------------------------------------------------- #
# Install / packaging
# --------------------------------------------------------------------------- #
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_install_targets bencode)
if(BENCODE_BUILD_CLI)
    list(APPEND _install_targets bencode_cli)
endif()

install(TARGETS ${_install_targets}
    EXPORT bencodeTargets
    RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/bencode
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/bencode/version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bencode
)

install(EXPORT bencodeTargets
    FILE bencodeTargets.cmake
    NAMESPACE bencode::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bencode
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bencodeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bencodeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bencodeConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bencode
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/bencodeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/bencodeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bencode
)

# Relocatable pkg-config -- the prefix is derived from this file's installed
# location at runtime, not baked in at configure time.
file(RELATIVE_PATH BENCODE_PC_RELATIVE_PREFIX
    "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig"
    "${CMAKE_INSTALL_PREFIX}"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bencode.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bencode.pc"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/bencode.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# --------------------------------------------------------------------------- #
# Summary
# --------------------------------------------------------------------------- #
message(STATUS "bencode ${PROJECT_VERSION}")
message(STATUS "  C standard:        C${CMAKE_C_STANDARD}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared library:    ${BUILD_SHARED_LIBS}")
message(STATUS "  CLI:               ${BENCODE_BUILD_CLI}")
message(STATUS "  Tests:             ${BENCODE_BUILD_TESTS}")
message(STATUS "  Examples:          ${BENCODE_BUILD_EXAMPLES}")
message(STATUS "  ASan/UBSan/MSan/TSan: ${BENCODE_ENABLE_ASAN}/${BENCODE_ENABLE_UBSAN}/${BENCODE_ENABLE_MSAN}/${BENCODE_ENABLE_TSAN}")
message(STATUS "  Coverage:          ${BENCODE_ENABLE_COVERAGE}")
