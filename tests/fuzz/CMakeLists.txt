# Fuzz target build. Clang-only; the gate lives in the top-level
# CMakeLists.txt which auto-disables BENCODE_BUILD_FUZZERS on non-Clang
# toolchains with a warning. Reaching this file means the check has
# already passed.

function(bencode_add_fuzz_target name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE bencode::bencode)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${name}    PRIVATE -fsanitize=fuzzer,address,undefined)
    bencode_apply_warnings(${name})
endfunction()

bencode_add_fuzz_target(fuzz_parse_sax)
bencode_add_fuzz_target(fuzz_parse_dom)
