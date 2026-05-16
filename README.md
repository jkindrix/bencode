# bencode

[![CI](https://github.com/jkindrix/bencode/actions/workflows/ci.yml/badge.svg)](https://github.com/jkindrix/bencode/actions/workflows/ci.yml)
[![CodeQL](https://github.com/jkindrix/bencode/actions/workflows/codeql.yml/badge.svg)](https://github.com/jkindrix/bencode/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C17](https://img.shields.io/badge/C-17-informational.svg)](https://www.iso.org/standard/74528.html)
[![CMake >= 3.20](https://img.shields.io/badge/CMake-%E2%89%A5%203.20-informational.svg)](CMakeLists.txt)

A strict [Bencode](https://wiki.theory.org/BitTorrentSpecification#Bencoding)
parser, emitter, and CLI in modern C.

Bencode is BitTorrent's encoding format. The library accepts only
strictly canonical input (no leading zeros, no `-0`, dict keys in
lexicographic order with no duplicates) and emits only canonical
output — so `decode → encode` is bit-exact for every accepted input.

## What's in the box

- **Two parser layers.** A SAX-style streaming parser that drives caller
  callbacks with zero allocation, and a DOM parser layered on top that
  builds an in-memory value tree.
- **Zero-copy by default.** Strings in a parsed tree point into the
  caller's input buffer. `bencode_value_clone()` produces an owned
  copy when needed.
- **An emitter** that writes canonical Bencode through a caller-supplied
  byte sink. Pairs with the DOM to give you `decode → emit` round-tripping.
- **A small CLI** (`bencode validate | print | roundtrip | size`) for
  shell-level use.
- **Strict input handling.** Every off-the-happy-path case has a
  specific error code: integer overflow, leading zeros, unsorted dict
  keys, duplicate keys, nesting that exceeds the depth limit, etc.

## Requirements

- A C17-capable compiler (GCC ≥ 9, Clang ≥ 9, MSVC ≥ 2019).
- CMake ≥ 3.20.
- Ninja recommended.

## Build & run

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/bencode validate examples/sample.torrent
```

Example output:

```text
$ printf 'd3:cow3:moo4:spaml1:a1:bee' | ./build/debug/bencode print -
{
  "cow": "moo",
  "spam": [
    "a",
    "b"
  ]
}

$ printf 'i03e' | ./build/debug/bencode validate -
./build/debug/bencode: malformed integer (leading zero, -0, or empty) at byte offset 2
```

Other presets:

```sh
cmake --preset release         && cmake --build --preset release
cmake --preset asan            && cmake --build --preset asan
cmake --preset tsan            && cmake --build --preset tsan
cmake --preset msan            && cmake --build --preset msan     # CC=clang required
cmake --preset coverage        && cmake --build --preset coverage
```

## CLI

| Subcommand    | Purpose                                                          |
|---------------|------------------------------------------------------------------|
| `validate`    | Read input, parse it, print `ok` on success.                     |
| `print`       | Parse input and print a human-readable rendering.                |
| `roundtrip`   | Parse then re-emit; assert the output is bit-exact to the input. |
| `size`        | Parse input and print structural statistics.                     |

`FILE` may be `-` or omitted to read stdin. Exit codes:

| Code | Meaning                                                                       |
|------|-------------------------------------------------------------------------------|
| 0    | Success.                                                                      |
| 1    | The input is not valid canonical Bencode, or the `roundtrip` output differed. |
| 2    | I/O error reading the input or writing to stdout, or an unknown option.       |

## Test

```sh
ctest --preset default
```

The suite covers integer edge cases (overflow positive and negative,
leading zero, `-0`, bare minus, empty), string edge cases (empty,
embedded NUL, length-too-long, leading-zero length, missing colon),
list and dict structure, dict ordering and duplicates, non-string dict
keys, trailing-byte handling, depth limits, NULL-argument rejection,
the builder API, status-string surjectivity, SAX callback abort, and a
roundtrip property test over a corpus of well-formed inputs.

## Install

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix /usr/local
```

Installs:

- `bin/bencode`         — the CLI
- `lib/libbencode.a`    — the static library (or `.so` with `BUILD_SHARED_LIBS=ON`)
- `include/bencode/*.h` — public headers
- `lib/cmake/bencode/`  — `find_package(bencode)` config files
- `lib/pkgconfig/bencode.pc` — relocatable pkg-config metadata

## Consuming

```cmake
find_package(bencode 0.2 REQUIRED)
target_link_libraries(my_app PRIVATE bencode::bencode)
```

Or as a subproject:

```cmake
add_subdirectory(third_party/bencode)
target_link_libraries(my_app PRIVATE bencode::bencode)
```

Non-CMake consumers can use `pkg-config`:

```sh
cc my_app.c $(pkg-config --cflags --libs bencode) -o my_app
```

## Public API at a glance

```c
#include <bencode/bencode.h>

/* SAX (streaming): */
bencode_status bencode_parse_sax(const uint8_t *input, size_t input_size,
                                 const bencode_callbacks *cb, void *user,
                                 size_t max_depth, size_t *consumed);

/* DOM: */
bencode_status bencode_parse(const uint8_t *input, size_t input_size,
                             const bencode_parse_options *opts,
                             bencode_value **out, size_t *consumed);
void           bencode_value_free(bencode_value *v);
bencode_status bencode_value_clone(const bencode_value *v,
                                   const bencode_allocator *a,
                                   bencode_value **out);

/* Accessors / builders: */
bencode_type   bencode_value_type(const bencode_value *v);
bencode_status bencode_value_int(const bencode_value *v, bencode_int_t *out);
bencode_status bencode_value_string(const bencode_value *v,
                                    const uint8_t **out_bytes, size_t *out_len);
size_t         bencode_list_size(const bencode_value *v);
const bencode_value *bencode_list_at(const bencode_value *v, size_t i);
size_t         bencode_dict_size(const bencode_value *v);
const bencode_value *bencode_dict_get(const bencode_value *v,
                                      const uint8_t *key, size_t key_len);
bencode_status bencode_dict_at(const bencode_value *v, size_t i,
                               const uint8_t **out_key, size_t *out_key_len,
                               const bencode_value **out_value);
bencode_status bencode_int_new(bencode_int_t v,
                               const bencode_allocator *a, bencode_value **out);
bencode_status bencode_string_new(const uint8_t *bytes, size_t len,
                                  const bencode_allocator *a, bencode_value **out);
bencode_status bencode_list_new(const bencode_allocator *a, bencode_value **out);
bencode_status bencode_list_append(bencode_value *list, bencode_value *child);
bencode_status bencode_dict_new(const bencode_allocator *a, bencode_value **out);
bencode_status bencode_dict_set(bencode_value *dict,
                                const uint8_t *key, size_t key_len,
                                bencode_value *child);

/* Emit: */
bencode_status bencode_emit(const bencode_value *v,
                            bencode_emit_fn emit, void *user);
bencode_status bencode_emit_to_file(const bencode_value *v, FILE *out);
bencode_status bencode_emit_to_alloc(const bencode_value *v,
                                     const bencode_allocator *a,
                                     uint8_t **out_bytes, size_t *out_len);
void           bencode_buffer_free(const bencode_allocator *a, uint8_t *bytes);

/* Misc: */
const char    *bencode_version(void);
const char    *bencode_status_string(bencode_status s);
```

All functions are thread-safe (no global state). Strings parsed from
input borrow into the input buffer; clone for owned copies. See
[`include/bencode/bencode.h`](include/bencode/bencode.h) for the full
per-function contracts.

## Versioning & ABI stability

`bencode` follows [Semantic Versioning](https://semver.org/):

- **Major** (`X.0.0`) — may break the public C API or ABI.
- **Minor** (`1.X.0`) — adds API surface without breaking existing
  callers. Shared-library `SOVERSION` is preserved.
- **Patch** (`1.0.X`) — bug fixes and documentation only.

**0.x exception.** Until 1.0.0, this project follows the standard
SemVer convention that the 0.x series is unstable. In particular,
**patch releases during 0.x may add new error-enum values or other
source-compatible additions** when needed to diagnose a fixed bug
(`BENCODE_ERR_DICT_MISSING_VALUE` was introduced in 0.2.1 alongside
the parser fix that produces it). Strict consumers compiling with
`-Wswitch-enum -Werror` should pin to an exact `0.x.y` during this
phase. The minor/patch discipline above kicks in once 1.0.0 ships.

The public surface is exactly the symbols declared in
`include/bencode/bencode.h` and tagged with `BENCODE_API`. Anything else
is private. The `shared-build` CI job runs a `symbol_export_check` test
on Linux that fails the build if a private symbol leaks into the
dynamic table.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `find_package(bencode)` not found. | Install prefix isn't on CMake's search path. | `-DCMAKE_PREFIX_PATH=<prefix>` or set `bencode_DIR`. |
| MSVC shared-link unresolved externals. | Consumer compiled without `dllimport`. | Define `BENCODE_USE_SHARED` before `#include <bencode/bencode.h>`. |
| `pkg-config --cflags --libs bencode` not found. | Install prefix's pkgconfig dir isn't on `PKG_CONFIG_PATH`. | `export PKG_CONFIG_PATH=<prefix>/lib/pkgconfig:$PKG_CONFIG_PATH`. |
| Sanitizer build aborts with `unexpected memory mapping`. | Kernel ≥ 6.x with Clang ≤ 15: ASLR vs sanitizer shadow. | Use Clang ≥ 16 (e.g. `CC=clang-19 cmake --preset asan`), or `sudo sysctl -w vm.mmap_rnd_bits=28`. Details in [CONTRIBUTING.md](CONTRIBUTING.md). |
| Fuzz target fails to link with `libclang_rt.fuzzer-*.a: No such file`. | Debian splits Clang's sanitizer/fuzzer runtimes. | `sudo apt install -y libclang-rt-19-dev` (matching your `clang` version). |

## Development

- Format: `scripts/format.sh` runs `clang-format -i` on all sources.
- Lint:   `scripts/lint.sh`   runs `clang-tidy` against `compile_commands.json`.
- Hooks:  `scripts/install-hooks.sh` wires `core.hooksPath` to `.githooks/`,
  enabling a `clang-format --dry-run -Werror` check on staged C sources
  at commit time.
- Coverage: `scripts/coverage.sh` computes coverage and enforces the
  v0.x 75 % floor (overridable with `MIN_LINE_COVERAGE=<pct>`), with
  lcov 1.x/2.x version detection. The floor will be ratcheted up as
  fault-injection tests are added for the NOMEM / I/O error paths.
- Fuzz:    `cmake --build build/fuzz --target fuzz_parse_sax fuzz_parse_dom`
  then `./build/fuzz/tests/fuzz/fuzz_parse_sax tests/fuzz/corpus -dict=tests/fuzz/bencode.dict -max_total_time=60`.
- CI:      see `.github/workflows/ci.yml` for the build/test matrix,
  sanitizer (ASan+UBSan, TSan, MSan), lint, coverage, fuzz, downstream
  consumer, and Doxygen jobs.

## License

[MIT](LICENSE).
