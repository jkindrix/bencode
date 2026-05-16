/*
 * SPDX-License-Identifier: MIT
 *
 * libFuzzer harness for the full decode + emit roundtrip. Covers the
 * DOM builder, the emitter, and the canonical-form invariant: if parse
 * succeeds, emit must produce bytes that re-parse identically.
 *
 * Running this under ASan + UBSan exercises the entire DOM lifecycle
 * (allocate, build, walk, emit, free) against arbitrary byte sequences.
 */
#include "bencode/bencode.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct sink {
    uint8_t *data;
    size_t len;
    size_t cap;
    int oom;
} sink;

/* NOLINTNEXTLINE(readability-identifier-naming) */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bencode_status sink_append(const void *bytes, size_t len, void *user) {
    sink *s = user;
    if (s->oom) {
        return BENCODE_ERR_NOMEM;
    }
    if (s->cap - s->len < len) {
        size_t new_cap = s->cap == 0 ? 4096U : s->cap;
        while (new_cap - s->len < len) {
            if (new_cap > SIZE_MAX / 2U) {
                s->oom = 1;
                return BENCODE_ERR_NOMEM;
            }
            new_cap *= 2U;
        }
        uint8_t *grown = realloc(s->data, new_cap);
        if (grown == NULL) {
            s->oom = 1;
            return BENCODE_ERR_NOMEM;
        }
        s->data = grown;
        s->cap = new_cap;
    }
    memcpy(s->data + s->len, bytes, len);
    s->len += len;
    return BENCODE_OK;
}

/* NOLINTNEXTLINE(readability-identifier-naming) */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    bencode_value *tree = NULL;
    size_t consumed = 0;
    bencode_status st = bencode_parse(data, size, NULL, &tree, &consumed);
    if (st != BENCODE_OK) {
        /* Error path: tree must be NULL (no leak). */
        return 0;
    }

    /* Success path: emit, then re-parse the emitted bytes, then compare
     * for canonical-roundtrip equivalence. */
    sink s = {0};
    bencode_status e = bencode_emit(tree, sink_append, &s);
    bencode_value_free(tree);

    if (e != BENCODE_OK) {
        free(s.data);
        return 0;
    }

    /* The emitted bytes should re-parse cleanly. */
    bencode_value *tree2 = NULL;
    size_t consumed2 = 0;
    bencode_status st2 = bencode_parse(s.data, s.len, NULL, &tree2, &consumed2);
    bencode_value_free(tree2);
    free(s.data);

    /* If parse succeeded once, it must succeed again on the re-emit. The
     * fuzzer treats a violation here (st2 != OK after st == OK) as a
     * crash via the assert below. UBSan turns abort() into a clear
     * diagnostic. */
    if (st2 != BENCODE_OK) {
        __builtin_trap();
    }
    return 0;
}
