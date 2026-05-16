# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-05-16

### Changed (breaking, gated by 0.x SemVer-exception)
- **`bencode_parse` is now strict-by-default.** The
  ::bencode_parse_options field `reject_trailing` has been renamed to
  `allow_trailing` and the default behavior flipped. Previously,
  passing NULL options (or a zero-initialized struct) accepted
  trailing bytes after the first complete value -- inconsistent with
  the README's "accepts only canonical input" claim. Now NULL /
  zero-init means strict (trailing bytes -> `BENCODE_ERR_UNEXPECTED_BYTE`).
  Callers parsing a stream of concatenated values must explicitly
  opt in with `opts.allow_trailing = 1`.

  Migration:
  - Old `opts.reject_trailing = 1` -> remove (now the default).
  - Old `opts.reject_trailing = 0` (or NULL opts) **with intent to
    accept trailing bytes** -> set `opts.allow_trailing = 1`.

### Fixed
- **Windows stdin was not binary-safe.** The CLI's `read_all`
  returned `stdin` directly for `-`/omitted-path input. On Windows
  that's text-mode: `\r\n` is translated to `\n` and `0x1A` (Ctrl-Z)
  is treated as EOF, corrupting binary Bencode payloads. The CLI
  now calls `_setmode(_fileno(stdin), _O_BINARY)` on `_WIN32` before
  reading. File input was already binary-safe via `fopen(..., "rb")`.
- `scripts/coverage.sh` no longer claims to enable branch coverage.
  The `--rc branch_coverage=1` / `--rc lcov_branch_coverage=1` flags
  produced no branch records under the project's current `--coverage`
  compile setup. Replaced the claim with a comment explaining what
  would be required to actually wire branches through; line coverage
  remains the enforced metric.

### Documentation
- ::bencode_parse / builder section gained an explicit "Depth
  contract" paragraph: the parser caps incoming nesting, the builder
  API does not, and ::bencode_emit / ::bencode_value_clone /
  ::bencode_value_free recurse, so callers building trees from
  untrusted input must validate depth themselves.

### Added
- Test `trailing_bytes_null_opts_rejected` covering the new
  strict-by-default contract for the most common call shape
  (passing NULL options).

## [0.2.2] - 2026-05-16

### Fixed
- **Fuzz builds did not actually instrument the library.** Each fuzz
  executable carried `-fsanitize=fuzzer,address,undefined`, but the
  `bencode` library it linked was compiled without any sanitizer
  flags. libFuzzer's coverage feedback and ASan/UBSan therefore only
  reached the trivial harness wrappers, not the parser. Build a
  separate `bencode_fuzz` static library inside `tests/fuzz/` with
  `-fsanitize=fuzzer-no-link,address,undefined` on every translation
  unit; the harnesses now link against it instead of the production
  `bencode::bencode` target. The production target stays
  uninstrumented for non-fuzz consumers.

  Observed throughput dropped from ~6M iter/s to ~300K iter/s on the
  SAX fuzzer when the library code is actually instrumented -- the
  earlier numbers were measuring near-empty wrappers.
- `bencode_allocator_check` doc said "called from every public entry
  point that takes an allocator," but `bencode_buffer_free` is
  `void`-returning and cannot fail. Tightened the comment to scope
  the claim to status-returning entry points and explain the
  caller-responsibility contract on `bencode_buffer_free`.

### Added
- 0.x SemVer-exception clause in README's "Versioning & ABI
  stability" section: until 1.0.0, patch releases may add new
  error-enum values (such as `BENCODE_ERR_DICT_MISSING_VALUE` in
  0.2.1) when they're needed to diagnose a fixed bug. Strict
  consumers compiling with `-Wswitch-enum -Werror` should pin to
  exact `0.x.y` versions during this phase.
- Test `partial_allocator_rejected` now exercises every public
  status-returning allocator-taking API (bencode_int_new,
  bencode_string_new, bencode_list_new, bencode_dict_new,
  bencode_parse, bencode_value_clone, bencode_emit_to_alloc), not
  just the int builder.

### Changed
- Replaced `(function pointer)1` casts in tests with dummy `static`
  no-op functions. Casting an integer literal to a function-pointer
  type was a clang-tidy-flagged portability smell even though the
  dummy pointers are never invoked.

## [0.2.1] - 2026-05-16

### Fixed
- **Parser correctness:** dictionaries with an odd number of body
  elements (a key with no matching value, e.g. `d1:ae`) were
  incorrectly accepted as valid canonical Bencode. The parser now
  returns the new ::BENCODE_ERR_DICT_MISSING_VALUE status whenever a
  dict closes while expecting a value. This is the canonical-form
  rule BEP 3 requires; the previous behavior was a spec violation.
- **Undefined behavior:** `bencode_parse_sax(NULL, 0, ...)` previously
  constructed pointer cursors from `NULL`, then compared them with `>=`
  and subtracted them, both of which are UB on null pointers under
  C17 6.5.8/4 even though most compilers treat the cases as defined.
  Zero-byte input now early-returns ::BENCODE_ERR_TRUNCATED before any
  pointer math.
- **Allocator validation:** partial `bencode_allocator` tables (one of
  `alloc` / `free` set, the other NULL) silently mixed a custom
  allocator with the platform `free` (or vice versa). The library now
  rejects partial tables with ::BENCODE_ERR_INVALID_ARG at every
  public entry point that takes an allocator. New test covers both
  half-set variants.
- Doc contradiction: the `bencode_value_type` Doxygen header said
  "Asserts internally that @p value is non-NULL" while the actual
  function returns ::BENCODE_INVALID for NULL. Header now matches the
  implementation; the contract is "returns BENCODE_INVALID for NULL,
  never asserts."

### Added
- ::BENCODE_ERR_DICT_MISSING_VALUE status code (14).
- Tests: `dict_missing_value_rejected` and
  `dict_partial_pair_after_complete_one_rejected` covering the parser
  fix; `partial_allocator_rejected` covering the allocator-table fix.

### Changed
- Coverage policy aligned across all three locations: CI workflow,
  `scripts/coverage.sh` default, and README all say 75 % (the v0.x
  baseline). Previously the script and README defaulted to 90 %
  while CI used 75 %, so running `scripts/coverage.sh build/coverage`
  locally would fail even though CI passed.

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
