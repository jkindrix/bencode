# Unit harness.
add_executable(test_bencode test_bencode.c)
target_link_libraries(test_bencode PRIVATE bencode::bencode)
bencode_apply_warnings(test_bencode)
bencode_apply_sanitizers(test_bencode)

add_test(NAME unit_tests COMMAND test_bencode)
set_tests_properties(unit_tests PROPERTIES
    FAIL_REGULAR_EXPRESSION "FAIL:"
    TIMEOUT 30
)

# -- CLI smoke tests -------------------------------------------------------- #

# Happy-path: pipe a known-good document on stdin, expect 'ok' + exit 0.
add_test(NAME cli_validate_stdin
    COMMAND bash -c "printf 'd3:cow3:moo4:spaml1:a1:bee' | $<TARGET_FILE:bencode_cli> validate -"
)
set_tests_properties(cli_validate_stdin PROPERTIES
    PASS_REGULAR_EXPRESSION "^ok\r?\n$"
    TIMEOUT 10
)

add_test(NAME cli_print_pretty
    COMMAND bash -c "printf 'd3:cow3:mooe' | $<TARGET_FILE:bencode_cli> print -"
)
set_tests_properties(cli_print_pretty PROPERTIES
    PASS_REGULAR_EXPRESSION "\"cow\": +\"moo\""
    TIMEOUT 10
)

add_test(NAME cli_roundtrip_canonical
    COMMAND bash -c "printf 'd3:cow3:moo4:spaml1:a1:bee' | $<TARGET_FILE:bencode_cli> roundtrip -"
)
set_tests_properties(cli_roundtrip_canonical PROPERTIES
    PASS_REGULAR_EXPRESSION "^ok\r?\n$"
    TIMEOUT 10
)

add_test(NAME cli_size_stats
    COMMAND bash -c "printf 'd3:cow3:mooe' | $<TARGET_FILE:bencode_cli> size -"
)
set_tests_properties(cli_size_stats PROPERTIES
    PASS_REGULAR_EXPRESSION "total_values: +2"
    TIMEOUT 10
)

add_test(NAME cli_version
    COMMAND bencode_cli --version
)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^bencode [0-9]+\\.[0-9]+\\.[0-9]+\r?\n$"
    TIMEOUT 10
)

add_test(NAME cli_help
    COMMAND bencode_cli --help
)
set_tests_properties(cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage:"
    TIMEOUT 10
)

# Error path: validate must exit 1 (not 0, not 2) on invalid bencode AND
# must mention the relevant error. WILL_FAIL + PASS_REGULAR_EXPRESSION
# don't compose cleanly (regex match marks the test passed, then
# WILL_FAIL inverts to failed), so wrap in a single shell command that
# asserts both conditions.
if(CMAKE_HOST_UNIX)
    add_test(NAME cli_validate_invalid_input
        COMMAND bash -c
          "out=$(printf 'i03e' | $<TARGET_FILE:bencode_cli> validate - 2>&1); \
           rc=$?; echo \"$out\"; \
           test $rc -eq 1 && echo \"$out\" | grep -q 'leading zero'"
    )
    set_tests_properties(cli_validate_invalid_input PROPERTIES TIMEOUT 10)

    # Unknown subcommand: exit 2 + "unknown subcommand" on stderr.
    add_test(NAME cli_unknown_subcommand
        COMMAND bash -c
          "out=$($<TARGET_FILE:bencode_cli> bogus 2>&1); rc=$?; \
           echo \"$out\"; test $rc -eq 2 && echo \"$out\" | grep -q 'unknown subcommand'"
    )
    set_tests_properties(cli_unknown_subcommand PROPERTIES TIMEOUT 10)

    # SIGPIPE -> exit 2. Read a small valid file, pipe `print` output into a
    # closed reader; default SIGPIPE handling is masked by the CLI itself
    # (signal(SIGPIPE, SIG_IGN)), so the documented "I/O error -> 2" holds.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME cli_broken_pipe_exit_code
            COMMAND bash -c
              "printf 'd3:cow3:mooe' | stdbuf -o0 $<TARGET_FILE:bencode_cli> print - | head -n 0; \
               test \${PIPESTATUS[1]} -eq 2"
        )
        set_tests_properties(cli_broken_pipe_exit_code PROPERTIES TIMEOUT 10)
    endif()
endif()

# -- Symbol export check (shared builds only) ------------------------------- #
# Asserts the dynamic symbol table is exactly the documented public API.
if(BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME symbol_export_check
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.sh"
            $<TARGET_FILE:bencode>
    )
    set_tests_properties(symbol_export_check PROPERTIES TIMEOUT 30)

    # Negative test: synthetic library with a leaked symbol must fail the
    # check. Validates the policy script itself.
    add_test(NAME symbol_export_check_negative
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/check_exports_negative.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.sh"
    )
    set_tests_properties(symbol_export_check_negative PROPERTIES TIMEOUT 30)
endif()

# -- Doxygen-coverage policy test (POSIX only; uses awk + bash) ------------ #
if(CMAKE_HOST_UNIX)
    add_test(NAME doxygen_coverage_check
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/check_doxygen_coverage.sh"
            "${CMAKE_SOURCE_DIR}/include/bencode/bencode.h"
    )
    set_tests_properties(doxygen_coverage_check PROPERTIES TIMEOUT 10)

    add_test(NAME doxygen_coverage_check_negative
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/check_doxygen_coverage_negative.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_doxygen_coverage.sh"
    )
    set_tests_properties(doxygen_coverage_check_negative PROPERTIES TIMEOUT 10)
endif()

# -- Bake sanitizer runtime options into every test so local `ctest` and CI
# -- see identical strictness. See §8 of the C project principles reference.
#
# Footgun: this enumerates tests defined in *this* directory only, and only
# those defined *above* this line. If a future contributor adds an
# add_subdirectory(more_tests) below here, those tests won't inherit the
# sanitizer environment.
get_property(_bencode_all_tests DIRECTORY PROPERTY TESTS)
if(BENCODE_ENABLE_ASAN OR BENCODE_ENABLE_UBSAN)
    foreach(_t IN LISTS _bencode_all_tests)
        set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:halt_on_error=1"
            "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
    endforeach()
endif()
if(BENCODE_ENABLE_TSAN)
    foreach(_t IN LISTS _bencode_all_tests)
        set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT
            "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1")
    endforeach()
endif()
if(BENCODE_ENABLE_MSAN)
    foreach(_t IN LISTS _bencode_all_tests)
        set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT
            "MSAN_OPTIONS=halt_on_error=1:print_stats=0")
    endforeach()
endif()
