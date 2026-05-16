#!/usr/bin/env bash
# Assert that every BENCODE_API declaration in the public header is
# preceded by a Doxygen /** ... */ comment block.
#
# Argument: path to bencode.h.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <path-to-bencode.h>" >&2
    exit 2
fi
hdr="$1"

# Track Doxygen blocks explicitly. A BENCODE_API line is documented iff a
# /** ... */ block (single-line or multi-line) closed immediately before
# it, with only blank lines in between. Plain `/* */` C comments and
# `#endif /* X */` markers do not count.
undoc=$(awk '
    BEGIN { in_doc = 0; doc_seen = 0 }
    /^[[:space:]]*\/\*\*.*\*\/[[:space:]]*$/ { doc_seen = 1; next }
    /^[[:space:]]*\/\*\*/ { in_doc = 1; next }
    in_doc && /\*\// { in_doc = 0; doc_seen = 1; next }
    in_doc { next }
    /^[[:space:]]*$/ { next }
    /^BENCODE_API/ {
        if (!doc_seen) { print FILENAME ":" NR ": " $0 }
        doc_seen = 0
        next
    }
    { doc_seen = 0 }
' "$hdr")

if [ -n "$undoc" ]; then
    echo "FAIL: BENCODE_API declaration without a preceding /** ... */ block:" >&2
    echo "$undoc" >&2
    exit 1
fi

count=$(grep -c '^BENCODE_API' "$hdr" || true)
echo "ok: ${count} BENCODE_API declaration(s) all carry a Doxygen comment"
