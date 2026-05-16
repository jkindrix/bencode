# Security Policy

## Supported versions

Only the latest minor release line receives security fixes.

| Version | Supported |
| ------- | --------- |
| 0.2.x   | ✅        |
| 0.1.x   | ❌        |

## Reporting a vulnerability

Please **do not** open a public GitHub issue for suspected security
problems. Use a [private security advisory](https://github.com/jkindrix/bencode/security/advisories/new)
with:

- A description of the issue and its impact (e.g., DoS, out-of-bounds
  read, infinite loop on crafted input).
- Steps to reproduce — ideally a minimal byte sequence that triggers
  the issue, plus the build configuration (Debug / Release /
  sanitizer / fuzzer).
- Affected version(s) and platform(s).

You will receive an acknowledgment within 72 hours. Once a fix is
available, we will coordinate a disclosure date with you and publish a
patched release.

## Hardening

The library is built with the following hardening features:

Compile-time (non-Debug):

- `_FORTIFY_SOURCE=3` when the toolchain supports it; fallback `=2`.
  Detection is performed at configure time via `check_c_source_compiles`.
- `-fstack-protector-strong`.
- `-fstrict-flex-arrays=3` when the toolchain supports it.
- `-fvisibility=hidden` plus an explicit `BENCODE_API` export macro.
- `-ffile-prefix-map` to strip absolute build paths from `__FILE__`
  strings and DWARF info.

Link-time (Linux executables):

- `-pie` (position-independent executables).
- `-Wl,-z,relro -Wl,-z,now` (full RELRO + immediate binding).
- `-Wl,-z,noexecstack`.

CI exercises every change under AddressSanitizer + UndefinedBehaviorSanitizer,
ThreadSanitizer, and MemorySanitizer (each in its own job, with
`halt_on_error=1` and `detect_leaks=1` where applicable). Two libFuzzer
harnesses (`fuzz_parse_sax`, `fuzz_parse_dom`) exercise the SAX parser
and the full decode + emit roundtrip on every push with a seed corpus
and token dictionary. CodeQL's `security-and-quality` query suite runs
weekly plus on every push/PR.
