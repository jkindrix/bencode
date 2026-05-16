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
