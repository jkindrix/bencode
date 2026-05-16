/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file parse_dom.c
 * @brief DOM parser: builds a value tree by driving the SAX parser.
 *
 * Strategy: the SAX parser invokes callbacks as values are recognized.
 * Each callback either constructs a leaf value and attaches it to the
 * current parent container (list or dict), or opens/closes a container
 * and updates the builder stack.
 *
 * Byte-string payloads in the produced tree alias the caller's input
 * buffer (zero-copy). Use ::bencode_value_clone to detach if needed.
 *
 * Dict ordering and uniqueness are already enforced by the SAX parser,
 * so the DOM layer can trust the keys arrive in order and won't
 * collide.
 */
#include "bencode/bencode.h"

#include "bencode_internal.h"

#include <stdint.h>
#include <stdlib.h>

/** One entry per open container on the build stack. */
typedef struct dom_frame {
    bencode_value *container;
    /** For dicts: 1 if the next on_string is a key, 0 if it's a value. */
    int expecting_key;
    /** For dicts: stashed key bytes from on_string-as-key, used when the
     *  next value (or container) arrives. Borrowed pointer into input. */
    const uint8_t *pending_key;
    size_t pending_key_len;
} dom_frame;

typedef struct dom_state {
    bencode_value *root;
    const bencode_allocator *alloc;
    /* +1 frame headroom because the SAX parser's own depth limit can
     * equal BENCODE_INTERNAL_MAX_DEPTH; we open a frame as we enter and
     * the array needs to fit it. Match-of-size to SAX's stack is
     * deliberate. */
    dom_frame frames[257];
    size_t depth;
    size_t depth_cap;
} dom_state;

/** Attach @p value to the current container, or set as root at top
 *  level. Takes ownership of @p value on success; on failure the caller
 *  must free it (or the cascade-free on s->root will take care of it
 *  if it was attached to an inner container before the failure point). */
static bencode_status attach(dom_state *s, bencode_value *value) {
    if (s->depth == 0) {
        /* Single top-level value enforced by SAX's parse_value (called
         * once) -- s->root should be NULL here. */
        s->root = value;
        return BENCODE_OK;
    }
    dom_frame *f = &s->frames[s->depth - 1];
    if (f->container->type == BENCODE_LIST) {
        return bencode_list_append(f->container, value);
    }
    /* DICT path: pending_key was set by a prior on_string-as-key. */
    bencode_status st = bencode_dict_set(f->container, f->pending_key, f->pending_key_len, value);
    f->pending_key = NULL;
    f->pending_key_len = 0;
    f->expecting_key = 1;
    return st;
}

static bencode_status dom_on_int(bencode_int_t value, void *user) {
    dom_state *s = user;
    bencode_value *node = NULL;
    bencode_status st = bencode_int_new(value, s->alloc, &node);
    if (st != BENCODE_OK) {
        return st;
    }
    st = attach(s, node);
    if (st != BENCODE_OK) {
        bencode_value_free(node);
    }
    return st;
}

static bencode_status dom_on_string(const uint8_t *bytes, size_t len, void *user) {
    dom_state *s = user;
    /* If we're inside a dict expecting a key, stash the bytes; the next
     * value-event will consume them via attach()'s dict path. */
    if (s->depth > 0) {
        dom_frame *f = &s->frames[s->depth - 1];
        if (f->container->type == BENCODE_DICT && f->expecting_key) {
            f->pending_key = bytes;
            f->pending_key_len = len;
            f->expecting_key = 0;
            return BENCODE_OK;
        }
    }
    /* Otherwise it's a value string -- borrow the bytes. */
    bencode_value *node = NULL;
    bencode_status st = bencode_string_new_borrowed(bytes, len, s->alloc, &node);
    if (st != BENCODE_OK) {
        return st;
    }
    st = attach(s, node);
    if (st != BENCODE_OK) {
        bencode_value_free(node);
    }
    return st;
}

static bencode_status open_container(dom_state *s, bencode_type type) {
    bencode_value *node = NULL;
    bencode_status st;
    if (type == BENCODE_LIST) {
        st = bencode_list_new(s->alloc, &node);
    } else {
        st = bencode_dict_new(s->alloc, &node);
    }
    if (st != BENCODE_OK) {
        return st;
    }
    /* Attach into the parent (or set root) before pushing -- once
     * attached, on the failure cleanup path the cascade-free on
     * s->root reclaims it. */
    st = attach(s, node);
    if (st != BENCODE_OK) {
        bencode_value_free(node);
        return st;
    }
    if (s->depth >= s->depth_cap) {
        return BENCODE_ERR_NESTING_TOO_DEEP;
    }
    s->frames[s->depth].container = node;
    s->frames[s->depth].expecting_key = (type == BENCODE_DICT) ? 1 : 0;
    s->frames[s->depth].pending_key = NULL;
    s->frames[s->depth].pending_key_len = 0;
    s->depth++;
    return BENCODE_OK;
}

static bencode_status dom_on_list_begin(void *user) {
    return open_container(user, BENCODE_LIST);
}

static bencode_status dom_on_dict_begin(void *user) {
    return open_container(user, BENCODE_DICT);
}

static bencode_status dom_on_list_end(void *user) {
    dom_state *s = user;
    /* SAX guarantees we're inside a list here; just pop. */
    if (s->depth > 0) {
        s->depth--;
    }
    return BENCODE_OK;
}

static bencode_status dom_on_dict_end(void *user) {
    dom_state *s = user;
    if (s->depth > 0) {
        s->depth--;
    }
    return BENCODE_OK;
}

bencode_status bencode_parse(const uint8_t *input, size_t input_size,
                             const bencode_parse_options *opts, bencode_value **out,
                             size_t *consumed) {
    if (out == NULL) {
        if (consumed != NULL) {
            *consumed = 0;
        }
        return BENCODE_ERR_INVALID_ARG;
    }
    *out = NULL;

    const bencode_allocator *alloc = (opts != NULL) ? opts->allocator : NULL;
    size_t max_depth = (opts != NULL) ? opts->max_depth : 0;
    /* Default (opts==NULL or opts->allow_trailing==0) is strict; trailing
     * bytes after the first complete value are rejected. Callers parsing a
     * stream of concatenated values must set opts->allow_trailing = 1. */
    int allow_trailing = (opts != NULL) ? opts->allow_trailing : 0;

    bencode_status as = bencode_allocator_check(alloc);
    if (as != BENCODE_OK) {
        if (consumed != NULL) {
            *consumed = 0;
        }
        return as;
    }

    dom_state state;
    state.root = NULL;
    state.alloc = alloc;
    state.depth = 0;
    /* Headroom matches the SAX internal cap; see the struct comment. */
    state.depth_cap = sizeof(state.frames) / sizeof(state.frames[0]);

    bencode_callbacks cb = {
        .on_int = dom_on_int,
        .on_string = dom_on_string,
        .on_list_begin = dom_on_list_begin,
        .on_list_end = dom_on_list_end,
        .on_dict_begin = dom_on_dict_begin,
        .on_dict_end = dom_on_dict_end,
    };

    size_t bytes_consumed = 0;
    bencode_status st =
        bencode_parse_sax(input, input_size, &cb, &state, max_depth, &bytes_consumed);

    if (st == BENCODE_OK && !allow_trailing && bytes_consumed != input_size) {
        st = BENCODE_ERR_UNEXPECTED_BYTE;
    }

    if (consumed != NULL) {
        *consumed = bytes_consumed;
    }

    if (st != BENCODE_OK) {
        bencode_value_free(state.root);
        return st;
    }

    *out = state.root;
    return BENCODE_OK;
}
