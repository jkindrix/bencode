# Fuzz target build. Clang-only; the gate lives in the top-level
# CMakeLists.txt which auto-disables BENCODE_BUILD_FUZZERS on non-Clang
# toolchains with a warning. Reaching this file means the check has
# already passed.
#
# Building strategy: we compile a *separate* copy of the library here
# with -fsanitize=fuzzer-no-link,address,undefined on every translation
# unit. Without this, the fuzz executables would link the regular
# `bencode` library target (which has no sanitizer instrumentation),
# and libFuzzer's coverage feedback + ASan/UBSan would only reach the
# tiny harness wrappers -- not the parser logic the fuzzers are
# nominally exercising. Building a second library target costs a few
# seconds at configure time and keeps the production `bencode` target
# uninstrumented for non-fuzz consumers.

set(BENCODE_FUZZ_SANITIZE -fsanitize=fuzzer-no-link,address,undefined)

# Resolve the library source list to absolute paths so the add_library
# call works from this subdirectory.
set(_fuzz_lib_sources)
foreach(_src IN LISTS BENCODE_LIB_SOURCES)
    list(APPEND _fuzz_lib_sources "${CMAKE_SOURCE_DIR}/${_src}")
endforeach()

add_library(bencode_fuzz STATIC ${_fuzz_lib_sources})
target_include_directories(bencode_fuzz
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_BINARY_DIR}/generated/include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
)
target_compile_features(bencode_fuzz PRIVATE c_std_17)
target_compile_options(bencode_fuzz PRIVATE
    ${BENCODE_FUZZ_SANITIZE}
    -fno-omit-frame-pointer
    -fvisibility=hidden
)
# The instrumentation tagging is read by libFuzzer + the sanitizer
# runtimes at link time; propagate to dependents so the harness's link
# step pulls in the right runtimes.
target_link_options(bencode_fuzz INTERFACE ${BENCODE_FUZZ_SANITIZE})
bencode_apply_warnings(bencode_fuzz)

function(bencode_add_fuzz_target name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE bencode_fuzz)
    # Executable gets the full `fuzzer` flag (provides libFuzzer's main);
    # the instrumented library was built with `fuzzer-no-link` so it
    # contributes coverage instrumentation without trying to bring in
    # its own main symbol.
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${name}    PRIVATE -fsanitize=fuzzer,address,undefined)
    bencode_apply_warnings(${name})
endfunction()

bencode_add_fuzz_target(fuzz_parse_sax)
bencode_add_fuzz_target(fuzz_parse_dom)
