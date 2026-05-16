/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file emit.c
 * @brief Canonical Bencode emitter.
 *
 * Walks a value tree and writes the canonical encoding through a
 * caller-supplied byte sink. "Canonical" means: integers without
 * leading zeros and without -0, byte strings as plain length-prefixed
 * bytes, dict entries emitted in lexicographic key order. The builder
 * API maintains dict ordering on insert so iterating entries in storage
 * order is correct here.
 */
#include "bencode/bencode.h"

#include "bencode_internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* -- Internal recursive emit ----------------------------------------------- */

static bencode_status emit_value(const bencode_value *value, bencode_emit_fn emit, void *user);

static bencode_status emit_bytes(bencode_emit_fn emit, void *user, const void *bytes, size_t len) {
    if (len == 0) {
        return BENCODE_OK;
    }
    return emit(bytes, len, user);
}

static bencode_status emit_int(const bencode_value *v, bencode_emit_fn emit, void *user) {
    /* Worst case is INT64_MIN with leading 'i' and trailing 'e':
     *   "i" + "-9223372036854775808" + "e" + NUL  ==  23 bytes. */
    char buf[32];
    int n = snprintf(buf, sizeof buf, "i%" PRId64 "e", v->as.i);
    if (n < 0 || (size_t)n >= sizeof buf) {
        return BENCODE_ERR_IO;
    }
    return emit_bytes(emit, user, buf, (size_t)n);
}

static bencode_status emit_string(const uint8_t *bytes, size_t len, bencode_emit_fn emit,
                                  void *user) {
    /* size_t fits in 20 decimal digits on 64-bit + ':' + NUL. */
    char header[32];
    int n = snprintf(header, sizeof header, "%zu:", len);
    if (n < 0 || (size_t)n >= sizeof header) {
        return BENCODE_ERR_IO;
    }
    bencode_status st = emit_bytes(emit, user, header, (size_t)n);
    if (st != BENCODE_OK) {
        return st;
    }
    return emit_bytes(emit, user, bytes, len);
}

static bencode_status emit_list(const bencode_value *v, bencode_emit_fn emit, void *user) {
    bencode_status st = emit_bytes(emit, user, "l", 1);
    if (st != BENCODE_OK) {
        return st;
    }
    for (size_t i = 0; i < v->as.list.count; ++i) {
        st = emit_value(v->as.list.items[i], emit, user);
        if (st != BENCODE_OK) {
            return st;
        }
    }
    return emit_bytes(emit, user, "e", 1);
}

static bencode_status emit_dict(const bencode_value *v, bencode_emit_fn emit, void *user) {
    bencode_status st = emit_bytes(emit, user, "d", 1);
    if (st != BENCODE_OK) {
        return st;
    }
    /* Entries are stored sorted; emit in storage order. */
    for (size_t i = 0; i < v->as.dict.count; ++i) {
        const bencode_dict_entry *e = &v->as.dict.entries[i];
        st = emit_string(e->key, e->key_len, emit, user);
        if (st != BENCODE_OK) {
            return st;
        }
        st = emit_value(e->value, emit, user);
        if (st != BENCODE_OK) {
            return st;
        }
    }
    return emit_bytes(emit, user, "e", 1);
}

static bencode_status emit_value(const bencode_value *value, bencode_emit_fn emit, void *user) {
    switch (value->type) {
    case BENCODE_INT:
        return emit_int(value, emit, user);
    case BENCODE_STRING:
        return emit_string(value->as.str.bytes, value->as.str.len, emit, user);
    case BENCODE_LIST:
        return emit_list(value, emit, user);
    case BENCODE_DICT:
        return emit_dict(value, emit, user);
    case BENCODE_INVALID:
    default:
        return BENCODE_ERR_INVALID_ARG;
    }
}

/* -- Public entry points --------------------------------------------------- */

bencode_status bencode_emit(const bencode_value *value, bencode_emit_fn emit, void *user) {
    if (value == NULL || emit == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    return emit_value(value, emit, user);
}

/* -- FILE* convenience ----------------------------------------------------- */

static bencode_status emit_to_file_cb(const void *bytes, size_t len, void *user) {
    FILE *fp = user;
    if (fwrite(bytes, 1, len, fp) != len) {
        return BENCODE_ERR_IO;
    }
    return BENCODE_OK;
}

bencode_status bencode_emit_to_file(const bencode_value *value, FILE *out) {
    if (value == NULL || out == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    bencode_status st = emit_value(value, emit_to_file_cb, out);
    if (st != BENCODE_OK) {
        return st;
    }
    if (fflush(out) != 0) {
        return BENCODE_ERR_IO;
    }
    return BENCODE_OK;
}

/* -- Emit-to-allocated-buffer convenience ----------------------------------- */

typedef struct alloc_sink {
    uint8_t *bytes;
    size_t len;
    size_t cap;
    const bencode_allocator *alloc;
    int oom;
} alloc_sink;

static bencode_status alloc_sink_append(const void *bytes, size_t len, void *user) {
    alloc_sink *s = user;
    if (s->oom) {
        return BENCODE_ERR_NOMEM;
    }
    /* Grow with explicit overflow guards. The allocator API gives us only
     * alloc + free, so we do alloc-new + memcpy + free-old. */
    if (s->cap - s->len < len) {
        size_t new_cap = s->cap == 0 ? 4096U : s->cap;
        while (new_cap - s->len < len) {
            if (new_cap > SIZE_MAX / 2U) {
                s->oom = 1;
                return BENCODE_ERR_NOMEM;
            }
            new_cap *= 2U;
        }
        uint8_t *fresh = bencode_alloc(s->alloc, new_cap);
        if (fresh == NULL) {
            s->oom = 1;
            return BENCODE_ERR_NOMEM;
        }
        if (s->bytes != NULL) {
            memcpy(fresh, s->bytes, s->len);
            bencode_free(s->alloc, s->bytes);
        }
        s->bytes = fresh;
        s->cap = new_cap;
    }
    memcpy(s->bytes + s->len, bytes, len);
    s->len += len;
    return BENCODE_OK;
}

bencode_status bencode_emit_to_alloc(const bencode_value *value, const bencode_allocator *alloc,
                                     uint8_t **out_bytes, size_t *out_len) {
    if (value == NULL || out_bytes == NULL || out_len == NULL) {
        if (out_bytes != NULL) {
            *out_bytes = NULL;
        }
        if (out_len != NULL) {
            *out_len = 0;
        }
        return BENCODE_ERR_INVALID_ARG;
    }
    *out_bytes = NULL;
    *out_len = 0;
    bencode_status as = bencode_allocator_check(alloc);
    if (as != BENCODE_OK) {
        return as;
    }

    alloc_sink s = {NULL, 0, 0, alloc, 0};
    bencode_status st = emit_value(value, alloc_sink_append, &s);
    if (st != BENCODE_OK) {
        bencode_free(alloc, s.bytes);
        return st;
    }
    *out_bytes = s.bytes;
    *out_len = s.len;
    return BENCODE_OK;
}

void bencode_buffer_free(const bencode_allocator *alloc, uint8_t *bytes) {
    bencode_free(alloc, bytes);
}
