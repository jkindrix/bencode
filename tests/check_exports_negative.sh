#!/usr/bin/env bash
# Negative test for check_exports.sh: build a synthetic shared library with
# a leaked symbol and verify the check correctly fails on it.
#
# Without this, a silent-pass bug in check_exports.sh would never be
# caught -- the positive test would still report "ok" against a real
# library while masking that the check itself is broken.
#
# Argument: path to check_exports.sh to test.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <path-to-check_exports.sh>" >&2
    exit 2
fi
checker="$1"

if ! command -v cc >/dev/null 2>&1; then
    echo "check_exports_negative: cc not in PATH; skipping" >&2
    exit 0
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

# A library that exports exactly the documented set PLUS one extra symbol.
# The extra symbol is the regression we want the checker to catch.
cat > "$tmpdir/lib.c" <<'EOF'
int bencode_buffer_free(void){return 0;}
int bencode_dict_at(void){return 0;}
int bencode_dict_get(void){return 0;}
int bencode_dict_new(void){return 0;}
int bencode_dict_set(void){return 0;}
int bencode_dict_size(void){return 0;}
int bencode_emit(void){return 0;}
int bencode_emit_to_alloc(void){return 0;}
int bencode_emit_to_file(void){return 0;}
int bencode_int_new(void){return 0;}
int bencode_list_append(void){return 0;}
int bencode_list_at(void){return 0;}
int bencode_list_new(void){return 0;}
int bencode_list_size(void){return 0;}
int bencode_parse(void){return 0;}
int bencode_parse_sax(void){return 0;}
int bencode_status_string(void){return 0;}
int bencode_string_new(void){return 0;}
int bencode_value_clone(void){return 0;}
int bencode_value_free(void){return 0;}
int bencode_value_int(void){return 0;}
int bencode_value_string(void){return 0;}
int bencode_value_type(void){return 0;}
int bencode_version(void){return 0;}
int bencode_accidentally_public(void){return 42;}
EOF

cc -shared -fPIC -o "$tmpdir/libfake.so" "$tmpdir/lib.c"

# Run the checker; expect failure (exit != 0). Capture stderr to ensure
# it mentions the leaked symbol -- mere non-zero exit isn't enough, the
# check could be broken in some other way.
set +e
out=$(bash "$checker" "$tmpdir/libfake.so" 2>&1)
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
    echo "FAIL: check_exports.sh accepted a library with a leaked symbol" >&2
    echo "      output: $out" >&2
    exit 1
fi
if ! echo "$out" | grep -q 'bencode_accidentally_public'; then
    echo "FAIL: check_exports.sh rejected the library but didn't name the leak" >&2
    echo "      output: $out" >&2
    exit 1
fi

echo "ok: check_exports.sh correctly rejected a library with a leaked symbol"
