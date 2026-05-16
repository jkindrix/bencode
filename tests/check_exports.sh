#!/usr/bin/env bash
# Assert that libbencode.so's dynamic symbol table is exactly the
# documented public BENCODE_API surface. Catches accidental leakage of
# private helpers (forgot to mark `static`, forgot to gate a helper that
# slipped into the header, etc.).
#
# Argument: path to libbencode.so.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <path-to-libbencode.so>" >&2
    exit 2
fi
lib="$1"

if ! command -v nm >/dev/null 2>&1; then
    echo "check_exports: nm not found in PATH; skipping" >&2
    exit 0
fi

# Keep this list sorted; it's diffed against `nm`'s output.
expected=$(printf '%s\n' \
    bencode_buffer_free \
    bencode_dict_at \
    bencode_dict_get \
    bencode_dict_new \
    bencode_dict_set \
    bencode_dict_size \
    bencode_emit \
    bencode_emit_to_alloc \
    bencode_emit_to_file \
    bencode_int_new \
    bencode_list_append \
    bencode_list_at \
    bencode_list_new \
    bencode_list_size \
    bencode_parse \
    bencode_parse_sax \
    bencode_status_string \
    bencode_string_new \
    bencode_value_clone \
    bencode_value_free \
    bencode_value_int \
    bencode_value_string \
    bencode_value_type \
    bencode_version \
    | sort)

actual=$(nm -D --defined-only --format=posix "$lib" \
    | awk '{print $1}' \
    | sed 's/@.*//' \
    | sort -u)

diff_out=$(diff <(echo "$expected") <(echo "$actual") || true)
if [ -n "$diff_out" ]; then
    echo "FAIL: libbencode.so exports an unexpected dynamic symbol set." >&2
    echo "      Lines starting with '<' are documented-but-missing," >&2
    echo "      '>' are present-but-undocumented." >&2
    echo "$diff_out" >&2
    exit 1
fi

count=$(echo "$expected" | wc -l)
echo "ok: dynamic symbol table = ${count} expected bencode_* exports"
