/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 *
 * Internal definitions shared across the library's .c files. Not installed,
 * not part of the public ABI.
 */
#ifndef BENCODE_INTERNAL_H_INCLUDED
#define BENCODE_INTERNAL_H_INCLUDED

#include "bencode/bencode.h"

#include <stddef.h>
#include <stdint.h>

/* -- Value layout ----------------------------------------------------------- */

typedef struct bencode_dict_entry {
    uint8_t *key; /**< Always owned (allocated by us). */
    size_t key_len;
    bencode_value *value;
} bencode_dict_entry;

struct bencode_value {
    bencode_type type;
    /** Allocator the caller supplied at construction. Borrowed; the caller
     *  must keep the bencode_allocator struct itself alive while any value
     *  built with it is alive. NULL means platform malloc/free. */
    const bencode_allocator *alloc;
    union {
        bencode_int_t i;
        struct {
            const uint8_t *bytes;
            size_t len;
            int owned; /**< 1 if `bytes` was allocated by us, 0 if borrowed. */
        } str;
        struct {
            bencode_value **items;
            size_t count;
            size_t capacity;
        } list;
        struct {
            bencode_dict_entry *entries;
            size_t count;
            size_t capacity;
        } dict;
    } as;
};

/* -- Allocator helpers (value.c) ------------------------------------------- */

void *bencode_alloc(const bencode_allocator *a, size_t size);
void bencode_free(const bencode_allocator *a, void *ptr);

/** Validate an allocator pointer. Returns ::BENCODE_OK if @p a is NULL
 *  or if both @p a->alloc and @p a->free are non-NULL; returns
 *  ::BENCODE_ERR_INVALID_ARG for a partial table. Called from every
 *  public entry point that takes a `const bencode_allocator *`. */
bencode_status bencode_allocator_check(const bencode_allocator *a);

/** Construct a string value whose payload pointer aliases @p bytes rather
 *  than being copied. Used by the DOM parser to keep its zero-copy promise.
 *  Not exposed publicly because borrowed-string lifetime is a footgun for
 *  general use. */
bencode_status bencode_string_new_borrowed(const uint8_t *bytes, size_t len,
                                           const bencode_allocator *alloc, bencode_value **out);

#endif /* BENCODE_INTERNAL_H_INCLUDED */
