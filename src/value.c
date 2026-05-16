/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file value.c
 * @brief In-memory value tree: type definition, lifecycle, accessors,
 *        builders. The DOM parser (parse_dom.c) and emitter (emit.c)
 *        both build on the primitives defined here.
 *
 * Memory model:
 *  - Each value stores a borrowed pointer to the allocator the caller
 *    supplied (or NULL for the platform default). The caller must keep
 *    that allocator's function table alive while any value built with
 *    it is alive.
 *  - For ::BENCODE_STRING, the byte payload can be either borrowed
 *    (pointer aliases the parser's input) or owned (allocated by us).
 *    The `str.owned` flag distinguishes; only owned bytes are freed.
 *  - For ::BENCODE_DICT, keys are always owned (allocated by us). The
 *    builder API needs key stability across the entry's lifetime, and
 *    storing borrowed keys only some of the time would complicate the
 *    contract without buying much.
 */
#include "bencode/bencode.h"

#include "bencode_internal.h"

#include <stdlib.h>
#include <string.h>

/* -- Allocator helpers ------------------------------------------------------ */

void *bencode_alloc(const bencode_allocator *a, size_t size) {
    if (a != NULL && a->alloc != NULL) {
        return a->alloc(size, a->user);
    }
    return malloc(size);
}

void bencode_free(const bencode_allocator *a, void *ptr) {
    if (ptr == NULL) {
        return;
    }
    if (a != NULL && a->free != NULL) {
        a->free(ptr, a->user);
        return;
    }
    free(ptr);
}

/* -- Type tag --------------------------------------------------------------- */

bencode_type bencode_value_type(const bencode_value *value) {
    /* The contract says assert internally; in release we still want a
     * defined return rather than UB on NULL, hence the fallback. */
    if (value == NULL) {
        return (bencode_type)0;
    }
    return value->type;
}

/* -- Lifecycle -------------------------------------------------------------- */

static bencode_value *value_alloc(const bencode_allocator *alloc, bencode_type type) {
    bencode_value *v = bencode_alloc(alloc, sizeof(*v));
    if (v == NULL) {
        return NULL;
    }
    memset(v, 0, sizeof(*v));
    v->type = type;
    v->alloc = alloc;
    return v;
}

void bencode_value_free(bencode_value *value) {
    if (value == NULL) {
        return;
    }
    switch (value->type) {
    case BENCODE_INT:
        break;
    case BENCODE_STRING:
        if (value->as.str.owned) {
            /* The bytes pointer is `const uint8_t *` so the public accessor
             * can return it directly; for owned payloads we allocated it
             * ourselves and now drop const to feed it to free. The
             * uintptr_t bounce silences -Wcast-qual. */
            /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
            bencode_free(value->alloc, (void *)(uintptr_t)value->as.str.bytes);
        }
        break;
    case BENCODE_LIST:
        for (size_t i = 0; i < value->as.list.count; ++i) {
            bencode_value_free(value->as.list.items[i]);
        }
        bencode_free(value->alloc, value->as.list.items);
        break;
    case BENCODE_DICT:
        for (size_t i = 0; i < value->as.dict.count; ++i) {
            bencode_free(value->alloc, value->as.dict.entries[i].key);
            bencode_value_free(value->as.dict.entries[i].value);
        }
        bencode_free(value->alloc, value->as.dict.entries);
        break;
    default:
        break;
    }
    bencode_free(value->alloc, value);
}

/* -- Builders --------------------------------------------------------------- */

bencode_status bencode_int_new(bencode_int_t v, const bencode_allocator *alloc,
                               bencode_value **out) {
    if (out == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;
    bencode_value *node = value_alloc(alloc, BENCODE_INT);
    if (node == NULL) {
        return BENCODE_ERR_NOMEM;
    }
    node->as.i = v;
    *out = node;
    return BENCODE_OK;
}

bencode_status bencode_string_new(const uint8_t *bytes, size_t len, const bencode_allocator *alloc,
                                  bencode_value **out) {
    if (out == NULL || (bytes == NULL && len != 0)) {
        if (out != NULL) {
            *out = NULL;
        }
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;
    bencode_value *node = value_alloc(alloc, BENCODE_STRING);
    if (node == NULL) {
        return BENCODE_ERR_NOMEM;
    }
    if (len == 0) {
        /* Pointer can be NULL or aliasing; we never read through it. We
         * still mark as owned=false so free is a no-op. */
        node->as.str.bytes = (const uint8_t *)"";
        node->as.str.len = 0;
        node->as.str.owned = 0;
    } else {
        uint8_t *copy = bencode_alloc(alloc, len);
        if (copy == NULL) {
            bencode_free(alloc, node);
            return BENCODE_ERR_NOMEM;
        }
        memcpy(copy, bytes, len);
        node->as.str.bytes = copy;
        node->as.str.len = len;
        node->as.str.owned = 1;
    }
    *out = node;
    return BENCODE_OK;
}

/* Build a string value that borrows the byte pointer rather than copying.
 * Used internally by the DOM parser; not exposed on the public API. */
bencode_status bencode_string_new_borrowed(const uint8_t *bytes, size_t len,
                                           const bencode_allocator *alloc, bencode_value **out) {
    if (out == NULL || (bytes == NULL && len != 0)) {
        if (out != NULL) {
            *out = NULL;
        }
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;
    bencode_value *node = value_alloc(alloc, BENCODE_STRING);
    if (node == NULL) {
        return BENCODE_ERR_NOMEM;
    }
    node->as.str.bytes = (len == 0) ? (const uint8_t *)"" : bytes;
    node->as.str.len = len;
    node->as.str.owned = 0;
    *out = node;
    return BENCODE_OK;
}

bencode_status bencode_list_new(const bencode_allocator *alloc, bencode_value **out) {
    if (out == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;
    bencode_value *node = value_alloc(alloc, BENCODE_LIST);
    if (node == NULL) {
        return BENCODE_ERR_NOMEM;
    }
    *out = node;
    return BENCODE_OK;
}

/** Grow a pointer-array by doubling. Replaces realloc since the
 *  allocator API only provides alloc/free. */
static bencode_status grow_array(const bencode_allocator *alloc, void **items_ptr,
                                 size_t element_size, size_t *capacity_ptr, size_t needed) {
    if (needed <= *capacity_ptr) {
        return BENCODE_OK;
    }
    size_t new_cap = *capacity_ptr == 0 ? 4 : *capacity_ptr;
    while (new_cap < needed) {
        /* Overflow guard. */
        if (new_cap > SIZE_MAX / 2U) {
            return BENCODE_ERR_NOMEM;
        }
        new_cap *= 2U;
    }
    if (new_cap > SIZE_MAX / element_size) {
        return BENCODE_ERR_NOMEM;
    }
    void *fresh = bencode_alloc(alloc, new_cap * element_size);
    if (fresh == NULL) {
        return BENCODE_ERR_NOMEM;
    }
    if (*items_ptr != NULL && *capacity_ptr > 0) {
        memcpy(fresh, *items_ptr, *capacity_ptr * element_size);
        bencode_free(alloc, *items_ptr);
    }
    *items_ptr = fresh;
    *capacity_ptr = new_cap;
    return BENCODE_OK;
}

bencode_status bencode_list_append(bencode_value *list, bencode_value *child) {
    if (list == NULL || child == NULL || list->type != BENCODE_LIST) {
        return BENCODE_ERR_INVALID_ARG;
    }
    void *items = list->as.list.items;
    bencode_status st = grow_array(list->alloc, &items, sizeof(bencode_value *),
                                   &list->as.list.capacity, list->as.list.count + 1);
    if (st != BENCODE_OK) {
        return st;
    }
    list->as.list.items = items;
    list->as.list.items[list->as.list.count++] = child;
    return BENCODE_OK;
}

bencode_status bencode_dict_new(const bencode_allocator *alloc, bencode_value **out) {
    if (out == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;
    bencode_value *node = value_alloc(alloc, BENCODE_DICT);
    if (node == NULL) {
        return BENCODE_ERR_NOMEM;
    }
    *out = node;
    return BENCODE_OK;
}

/** Binary-search the dict for @p key. Returns the index where it lives
 *  (or would be inserted) in @p out_index; sets @p out_match to 1 iff
 *  an exact match exists. */
static void dict_search(const bencode_value *dict, const uint8_t *key, size_t key_len,
                        size_t *out_index, int *out_match) {
    size_t lo = 0;
    size_t hi = dict->as.dict.count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        const bencode_dict_entry *e = &dict->as.dict.entries[mid];
        size_t n = e->key_len < key_len ? e->key_len : key_len;
        int c = n == 0 ? 0 : memcmp(e->key, key, n);
        if (c == 0) {
            if (e->key_len < key_len) {
                c = -1;
            } else if (e->key_len > key_len) {
                c = 1;
            }
        }
        if (c == 0) {
            *out_index = mid;
            *out_match = 1;
            return;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *out_index = lo;
    *out_match = 0;
}

bencode_status bencode_dict_set(bencode_value *dict, const uint8_t *key, size_t key_len,
                                bencode_value *child) {
    if (dict == NULL || child == NULL || dict->type != BENCODE_DICT ||
        (key == NULL && key_len != 0)) {
        return BENCODE_ERR_INVALID_ARG;
    }
    size_t index = 0;
    int match = 0;
    dict_search(dict, key, key_len, &index, &match);
    if (match) {
        /* Replace the existing value, free the old one. The key buffer
         * stays put. */
        bencode_value_free(dict->as.dict.entries[index].value);
        dict->as.dict.entries[index].value = child;
        return BENCODE_OK;
    }

    /* Allocate the key copy *before* growing the entries array, so we
     * can leave the caller's child untouched if the key allocation fails. */
    uint8_t *key_copy = NULL;
    if (key_len > 0) {
        key_copy = bencode_alloc(dict->alloc, key_len);
        if (key_copy == NULL) {
            return BENCODE_ERR_NOMEM;
        }
        memcpy(key_copy, key, key_len);
    }

    void *entries = dict->as.dict.entries;
    bencode_status st = grow_array(dict->alloc, &entries, sizeof(bencode_dict_entry),
                                   &dict->as.dict.capacity, dict->as.dict.count + 1);
    if (st != BENCODE_OK) {
        bencode_free(dict->alloc, key_copy);
        return st;
    }
    dict->as.dict.entries = entries;

    /* Shift entries above `index` up by one to make room. */
    if (index < dict->as.dict.count) {
        memmove(&dict->as.dict.entries[index + 1], &dict->as.dict.entries[index],
                (dict->as.dict.count - index) * sizeof(bencode_dict_entry));
    }
    dict->as.dict.entries[index].key = key_copy;
    dict->as.dict.entries[index].key_len = key_len;
    dict->as.dict.entries[index].value = child;
    dict->as.dict.count++;
    return BENCODE_OK;
}

/* -- Accessors -------------------------------------------------------------- */

bencode_status bencode_value_int(const bencode_value *value, bencode_int_t *out) {
    if (value == NULL || out == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    if (value->type != BENCODE_INT) {
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = value->as.i;
    return BENCODE_OK;
}

bencode_status bencode_value_string(const bencode_value *value, const uint8_t **out_bytes,
                                    size_t *out_len) {
    if (value == NULL || out_bytes == NULL || out_len == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    if (value->type != BENCODE_STRING) {
        return BENCODE_ERR_INVALID_ARG;
    }
    *out_bytes = value->as.str.bytes;
    *out_len = value->as.str.len;
    return BENCODE_OK;
}

size_t bencode_list_size(const bencode_value *value) {
    if (value == NULL || value->type != BENCODE_LIST) {
        return 0;
    }
    return value->as.list.count;
}

const bencode_value *bencode_list_at(const bencode_value *value, size_t index) {
    if (value == NULL || value->type != BENCODE_LIST || index >= value->as.list.count) {
        return NULL;
    }
    return value->as.list.items[index];
}

size_t bencode_dict_size(const bencode_value *value) {
    if (value == NULL || value->type != BENCODE_DICT) {
        return 0;
    }
    return value->as.dict.count;
}

const bencode_value *bencode_dict_get(const bencode_value *value, const uint8_t *key,
                                      size_t key_len) {
    if (value == NULL || value->type != BENCODE_DICT || (key == NULL && key_len != 0)) {
        return NULL;
    }
    size_t index = 0;
    int match = 0;
    dict_search(value, key, key_len, &index, &match);
    if (!match) {
        return NULL;
    }
    return value->as.dict.entries[index].value;
}

bencode_status bencode_dict_at(const bencode_value *value, size_t index, const uint8_t **out_key,
                               size_t *out_key_len, const bencode_value **out_value) {
    if (value == NULL || value->type != BENCODE_DICT || index >= value->as.dict.count) {
        return BENCODE_ERR_INVALID_ARG;
    }
    const bencode_dict_entry *e = &value->as.dict.entries[index];
    if (out_key != NULL) {
        *out_key = e->key;
    }
    if (out_key_len != NULL) {
        *out_key_len = e->key_len;
    }
    if (out_value != NULL) {
        *out_value = e->value;
    }
    return BENCODE_OK;
}

/* -- Clone ------------------------------------------------------------------ */

bencode_status bencode_value_clone(const bencode_value *value, const bencode_allocator *alloc,
                                   bencode_value **out) {
    if (out == NULL) {
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (value == NULL) {
        return BENCODE_OK;
    }

    bencode_status st;
    switch (value->type) {
    case BENCODE_INT:
        return bencode_int_new(value->as.i, alloc, out);

    case BENCODE_STRING:
        return bencode_string_new(value->as.str.bytes, value->as.str.len, alloc, out);

    case BENCODE_LIST: {
        bencode_value *list = NULL;
        st = bencode_list_new(alloc, &list);
        if (st != BENCODE_OK) {
            return st;
        }
        for (size_t i = 0; i < value->as.list.count; ++i) {
            bencode_value *child = NULL;
            st = bencode_value_clone(value->as.list.items[i], alloc, &child);
            if (st != BENCODE_OK) {
                bencode_value_free(list);
                return st;
            }
            st = bencode_list_append(list, child);
            if (st != BENCODE_OK) {
                bencode_value_free(child);
                bencode_value_free(list);
                return st;
            }
        }
        *out = list;
        return BENCODE_OK;
    }

    case BENCODE_DICT: {
        bencode_value *dict = NULL;
        st = bencode_dict_new(alloc, &dict);
        if (st != BENCODE_OK) {
            return st;
        }
        for (size_t i = 0; i < value->as.dict.count; ++i) {
            const bencode_dict_entry *e = &value->as.dict.entries[i];
            bencode_value *child = NULL;
            st = bencode_value_clone(e->value, alloc, &child);
            if (st != BENCODE_OK) {
                bencode_value_free(dict);
                return st;
            }
            st = bencode_dict_set(dict, e->key, e->key_len, child);
            if (st != BENCODE_OK) {
                bencode_value_free(child);
                bencode_value_free(dict);
                return st;
            }
        }
        *out = dict;
        return BENCODE_OK;
    }

    default:
        return BENCODE_ERR_INVALID_ARG;
    }
}
