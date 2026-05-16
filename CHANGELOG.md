# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-05-16

### Added
- `bencode_emit_to_alloc` + `bencode_buffer_free`: convenience API for
  emitting a value tree into a freshly-allocated byte buffer. Mirrors
  the allocator the caller passed (or stdlib malloc/free when NULL).
  The `roundtrip` CLI subcommand was the first internal user of the
  pattern; the public function eliminates the duplication.
- `BENCODE_INVALID = 0` enumerator in `bencode_type`.
  `bencode_value_type(NULL)` now returns `BENCODE_INVALID` instead of
  the previous `BENCODE_INT` fallback. Callers using `switch` on the
  return value can dispatch the NULL case via the `default:` arm
  without a separate guard.
- `benchmarks/bench.c`: `bencode_bench` microbenchmark target.
  Generates a synthetic in-memory corpus and reports MB/s and
  iter/sec for `parse_sax`, `parse_dom`, `emit`, and `roundtrip` over
  a configurable wallclock budget. Built behind the new
  `BENCODE_BUILD_BENCHMARKS` option.
- Public-header docs: complexity note on `bencode_dict_set`
  (`O(log n)` search + `O(n)` memmove for the ordered insert; bulk
  construction is `O(n²)`) and stack-cost note on `bencode_parse`
  (~6 KB scope stack).
- Tests for `bencode_emit_to_alloc` (basic + NULL-arg rejection) and
  for `bencode_value_type(NULL) == BENCODE_INVALID`.

### Changed
- `bencode_value_type(NULL)` returns `BENCODE_INVALID` (was
  `BENCODE_INT`). Pre-0.2 callers that relied on the old fallback
  must add a NULL guard if they cared. Source-compatible with any
  caller that already NULL-checks (the recommended pattern).
- Two `bencode_value **` ↔ `void *` assignments in `value.c` now
  carry explicit casts to satisfy clang-tidy 18's
  `bugprone-multi-level-implicit-pointer-conversion` check. No
  behavior change.
- `bencode_value_type(NULL)` fallback no longer fabricates an
  out-of-range enum value (was tripping
  `clang-analyzer-optin.core.EnumCastOutOfRange` on clang-tidy 18+).

### Fixed
- `bencode_parse_sax` no longer performs `NULL + 0` pointer
  arithmetic when called with `input == NULL && input_size == 0` (UB
  caught by UBSan on the first sanitizer build; defined-zero-bytes
  parse now returns `BENCODE_ERR_TRUNCATED` cleanly).
- Stale `HELLO_ERR_IO` reference in `src/main.c` -- find/replace
  artifact from copying the CLI exit-code rationale across projects.

## [0.1.0] - 2026-05-16

### Added
- Public C API: `bencode_parse_sax`, `bencode_parse`, `bencode_value_*`,
  `bencode_list_*`, `bencode_dict_*`, `bencode_int_new`,
  `bencode_string_new`, `bencode_list_new`, `bencode_list_append`,
  `bencode_dict_new`, `bencode_dict_set`, `bencode_emit`,
  `bencode_emit_to_file`, `bencode_value_clone`, `bencode_version`,
  `bencode_status_string`.
- SAX-style streaming parser with zero-allocation callback dispatch.
- DOM parser layered on the SAX parser; zero-copy strings borrow into
  the input buffer.
- Canonical emitter; `decode → encode` is bit-exact for every accepted
  input.
- Strict canonical validation: integer leading-zero / `-0` / bare-minus
  / empty rejection, dict-key ordering and uniqueness enforcement,
  non-string-key rejection, configurable nesting-depth limit (default
  64).
- `bencode` CLI with `validate`, `print`, `roundtrip`, and `size`
  subcommands; stable exit codes (0/1/2); `SIGPIPE` handled so the
  exit-code contract holds under default shell behavior.
- Modern CMake build (≥ 3.20): target-based, alias `bencode::bencode`,
  install + export with `find_package(bencode)`, relocatable
  `pkg-config` via `${pcfiledir}`, generated `version.h`.
- Sanitizer wiring for ASan/UBSan, TSan, MSan with mutual-exclusion
  guards. Sanitizer runtime options baked into CTest properties.
- Compile + link hardening: auto-detected `_FORTIFY_SOURCE=3` with `=2`
  fallback, `-fstack-protector-strong`, `-fstrict-flex-arrays=3` (when
  supported), `-ffile-prefix-map` for reproducible binaries, PIE +
  RELRO + BIND_NOW + noexecstack on Linux executables.
- libFuzzer harnesses: `fuzz_parse_sax` (SAX-only) and `fuzz_parse_dom`
  (full decode + emit roundtrip). Seed corpus + token dictionary.
- Policy-as-test scripts: `check_exports.sh` (dynamic symbol table ==
  documented `BENCODE_API` surface) and `check_doxygen_coverage.sh`
  (every `BENCODE_API` declaration carries a Doxygen block), each with
  a negative-test that confirms the script itself fails as expected.
- Unit + CLI smoke + property test harness (~40 tests).
- README, CHANGELOG, CONTRIBUTING, SECURITY, SUPPORT, CODE_OF_CONDUCT.
- GitHub Actions CI: Linux × macOS × Windows × {GCC, Clang, MSVC} ×
  {Debug, Release}, plus dedicated ASan+UBSan, TSan, MSan, lint,
  coverage, shared-build + symbol-export, install-consumer, fuzz, and
  Doxygen jobs. Top-level `permissions: contents: read`; every action
  SHA-pinned with a version comment.
- Dependabot configured to group GitHub Actions bumps into one PR per
  week.
- `scripts/format.sh`, `scripts/lint.sh`, `scripts/coverage.sh`,
  `scripts/install-hooks.sh`, `.githooks/pre-commit`.
