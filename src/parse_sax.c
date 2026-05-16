/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file parse_sax.c
 * @brief Strict Bencode SAX parser.
 *
 * Single-pass, allocation-free. Validates the canonical-Bencode rules:
 * integers have no leading zeros and no negative zero, byte-string length
 * prefixes have no leading zeros, dict keys must be byte strings, dict
 * keys must be in lexicographic order, and dict keys must be unique.
 *
 * The parser keeps a small fixed-size scope stack so it can track the
 * previous dict key at each open level without allocating. Inputs deeper
 * than ::BENCODE_INTERNAL_MAX_DEPTH are rejected with
 * ::BENCODE_ERR_NESTING_TOO_DEEP regardless of the caller's @p max_depth
 * (which can be smaller but not larger).
 */
#include "bencode/bencode.h"

#include <stdint.h>
#include <string.h>

/** Internal hard cap on nesting depth. Generous enough to accept any
 *  realistic torrent file; small enough to keep the scope-stack on
 *  the C stack at a sensible size (a few KB). */
#define BENCODE_INTERNAL_MAX_DEPTH 256

typedef struct sax_scope {
    /** 1 if the open scope at this level is a dict; 0 if it's a list. */
    int is_dict;
    /** For dicts: 1 if the next emitted item should be a key, 0 if a value. */
    int expecting_key;
    /** For dicts: pointer + length of the most-recent key, used to enforce
     *  ordering and uniqueness. NULL until the first key arrives. */
    const uint8_t *last_key;
    size_t last_key_len;
} sax_scope;

typedef struct sax_state {
    const uint8_t *p;      /**< Current input cursor. */
    const uint8_t *end;    /**< One past the last input byte. */
    const uint8_t *origin; /**< First input byte (for error-offset reporting). */
    const bencode_callbacks *cb;
    void *user;
    size_t depth;     /**< Current nesting depth; 0 = top level. */
    size_t max_depth; /**< Effective limit, after clamping. */
    sax_scope scopes[BENCODE_INTERNAL_MAX_DEPTH];
} sax_state;

/* Forward decls. */
static bencode_status parse_value(sax_state *s);
static bencode_status parse_int(sax_state *s);
static bencode_status parse_string(sax_state *s, const uint8_t **out_bytes, size_t *out_len);
static bencode_status parse_list(sax_state *s);
static bencode_status parse_dict(sax_state *s);

/* -- Helpers --------------------------------------------------------------- */

static int at_end(const sax_state *s) {
    return s->p >= s->end;
}

static uint8_t peek(const sax_state *s) {
    return *s->p;
}

static int is_digit(uint8_t b) {
    return b >= '0' && b <= '9';
}

/** Lexicographic comparison of two byte strings. Returns < 0, 0, or > 0.
 *  Shorter string is considered less when the common prefix is equal. */
static int bytes_cmp(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    int c = (n == 0) ? 0 : memcmp(a, b, n);
    if (c != 0) {
        return c;
    }
    if (alen < blen) {
        return -1;
    }
    if (alen > blen) {
        return 1;
    }
    return 0;
}

/* -- Integer parsing -------------------------------------------------------- */

/**
 * Parse a base-10 unsigned magnitude into @p out_mag. Stops on the first
 * non-digit byte. The bencode integer format allows leading zeros only
 * for the literal "0"; this function enforces that rule.
 *
 * @param s         Parser state; @p s->p must point at the first digit.
 * @param[out] out_mag    Receives the parsed magnitude.
 * @param max_mag   Maximum allowed magnitude (for signed-range checking).
 *
 * @return ::BENCODE_OK on success, ::BENCODE_ERR_INTEGER_FORMAT for
 *         empty or leading-zero input, ::BENCODE_ERR_INTEGER_OVERFLOW
 *         on numeric overflow, ::BENCODE_ERR_TRUNCATED on EOF.
 */
static bencode_status parse_unsigned(sax_state *s, uint64_t *out_mag, uint64_t max_mag) {
    if (at_end(s) || !is_digit(peek(s))) {
        /* Caller distinguishes "empty" from "EOF" by context. */
        return at_end(s) ? BENCODE_ERR_TRUNCATED : BENCODE_ERR_INTEGER_FORMAT;
    }

    uint8_t first = peek(s);
    s->p++;
    uint64_t acc = (uint64_t)(first - '0');

    /* Leading zero: only "0" by itself is legal. If the next byte is a
     * digit, the literal had a leading zero and is rejected. */
    if (first == '0' && !at_end(s) && is_digit(peek(s))) {
        return BENCODE_ERR_INTEGER_FORMAT;
    }

    while (!at_end(s) && is_digit(peek(s))) {
        uint64_t digit = (uint64_t)(peek(s) - '0');
        /* Overflow guard: acc*10 + digit > max_mag iff acc > (max - d) / 10. */
        if (acc > (max_mag - digit) / 10U) {
            return BENCODE_ERR_INTEGER_OVERFLOW;
        }
        acc = acc * 10U + digit;
        s->p++;
    }

    *out_mag = acc;
    return BENCODE_OK;
}

/** Parse `i<digits>e`. Cursor is at the 'i' on entry, one past 'e' on
 *  successful exit. */
static bencode_status parse_int(sax_state *s) {
    /* Caller verified peek(s) == 'i'. */
    s->p++;

    int negative = 0;
    if (!at_end(s) && peek(s) == '-') {
        negative = 1;
        s->p++;
    }

    /* Bare minus -- "i-e". */
    if (at_end(s)) {
        return BENCODE_ERR_TRUNCATED;
    }
    if (!is_digit(peek(s))) {
        return BENCODE_ERR_INTEGER_FORMAT;
    }

    /* The largest legal magnitude is |INT64_MIN| = 2^63 for negatives,
     * 2^63 - 1 for non-negatives. */
    const uint64_t pos_max = (uint64_t)INT64_MAX;
    const uint64_t neg_max = (uint64_t)INT64_MAX + 1U;
    uint64_t mag = 0;
    bencode_status st = parse_unsigned(s, &mag, negative ? neg_max : pos_max);
    if (st != BENCODE_OK) {
        return st;
    }

    /* "-0" is forbidden. */
    if (negative && mag == 0) {
        return BENCODE_ERR_INTEGER_FORMAT;
    }

    if (at_end(s) || peek(s) != 'e') {
        return at_end(s) ? BENCODE_ERR_TRUNCATED : BENCODE_ERR_UNEXPECTED_BYTE;
    }
    s->p++;

    bencode_int_t value;
    if (negative) {
        /* INT64_MIN can't be represented as a positive int64_t, so a
         * direct negation of (int64_t)mag would be UB for the boundary.
         * Special-case it. */
        if (mag == neg_max) {
            value = INT64_MIN;
        } else {
            value = -(bencode_int_t)mag;
        }
    } else {
        value = (bencode_int_t)mag;
    }

    if (s->cb != NULL && s->cb->on_int != NULL) {
        return s->cb->on_int(value, s->user);
    }
    return BENCODE_OK;
}

/* -- String parsing --------------------------------------------------------- */

/** Parse `<length>:<bytes>` and return a borrowed pointer to the bytes.
 *  Cursor is at the first digit on entry, one past the last payload byte
 *  on successful exit. */
static bencode_status parse_string(sax_state *s, const uint8_t **out_bytes, size_t *out_len) {
    /* Length is unsigned; uint64 magnitude with leading-zero rule. The
     * cap is SIZE_MAX because we'll need to compare against (end - p)
     * which is at most SIZE_MAX. */
    uint64_t mag = 0;
    bencode_status st = parse_unsigned(s, &mag, (uint64_t)SIZE_MAX);
    if (st != BENCODE_OK) {
        return st;
    }
    if (at_end(s) || peek(s) != ':') {
        return at_end(s) ? BENCODE_ERR_TRUNCATED : BENCODE_ERR_UNEXPECTED_BYTE;
    }
    s->p++;

    size_t len = (size_t)mag;
    /* Bounds check: how many bytes remain? */
    size_t remaining = (size_t)(s->end - s->p);
    if (len > remaining) {
        return BENCODE_ERR_STRING_LENGTH;
    }

    const uint8_t *bytes = s->p;
    s->p += len;

    if (out_bytes != NULL) {
        *out_bytes = bytes;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return BENCODE_OK;
}

/* -- Container parsing ------------------------------------------------------ */

/** Push a new scope; returns BENCODE_ERR_NESTING_TOO_DEEP if at the
 *  configured limit. */
static bencode_status push_scope(sax_state *s, int is_dict) {
    if (s->depth >= s->max_depth) {
        return BENCODE_ERR_NESTING_TOO_DEEP;
    }
    sax_scope *sc = &s->scopes[s->depth];
    sc->is_dict = is_dict;
    sc->expecting_key = is_dict ? 1 : 0;
    sc->last_key = NULL;
    sc->last_key_len = 0;
    s->depth++;
    return BENCODE_OK;
}

static void pop_scope(sax_state *s) {
    /* Caller must guarantee depth > 0. */
    s->depth--;
}

static bencode_status parse_list(sax_state *s) {
    /* Caller verified peek(s) == 'l'. */
    s->p++;

    bencode_status st = push_scope(s, /*is_dict=*/0);
    if (st != BENCODE_OK) {
        return st;
    }

    if (s->cb != NULL && s->cb->on_list_begin != NULL) {
        st = s->cb->on_list_begin(s->user);
        if (st != BENCODE_OK) {
            pop_scope(s);
            return st;
        }
    }

    while (1) {
        if (at_end(s)) {
            pop_scope(s);
            return BENCODE_ERR_TRUNCATED;
        }
        if (peek(s) == 'e') {
            s->p++;
            pop_scope(s);
            if (s->cb != NULL && s->cb->on_list_end != NULL) {
                return s->cb->on_list_end(s->user);
            }
            return BENCODE_OK;
        }
        st = parse_value(s);
        if (st != BENCODE_OK) {
            pop_scope(s);
            return st;
        }
    }
}

/** Parse and validate one dict key. On success, updates @p sc with the
 *  new last-key info and clears its expecting_key flag, and notifies the
 *  on_string callback. */
static bencode_status parse_dict_key(sax_state *s, sax_scope *sc) {
    if (!is_digit(peek(s))) {
        return BENCODE_ERR_DICT_BAD_KEY;
    }
    const uint8_t *key_bytes = NULL;
    size_t key_len = 0;
    bencode_status st = parse_string(s, &key_bytes, &key_len);
    if (st != BENCODE_OK) {
        return st;
    }
    /* Order + uniqueness check against the previous key (if any). */
    if (sc->last_key != NULL) {
        int c = bytes_cmp(sc->last_key, sc->last_key_len, key_bytes, key_len);
        if (c == 0) {
            return BENCODE_ERR_DICT_DUPLICATE;
        }
        if (c > 0) {
            return BENCODE_ERR_DICT_UNSORTED;
        }
    }
    sc->last_key = key_bytes;
    sc->last_key_len = key_len;
    sc->expecting_key = 0;

    if (s->cb != NULL && s->cb->on_string != NULL) {
        return s->cb->on_string(key_bytes, key_len, s->user);
    }
    return BENCODE_OK;
}

static bencode_status parse_dict(sax_state *s) {
    /* Caller verified peek(s) == 'd'. */
    s->p++;

    bencode_status st = push_scope(s, /*is_dict=*/1);
    if (st != BENCODE_OK) {
        return st;
    }

    if (s->cb != NULL && s->cb->on_dict_begin != NULL) {
        st = s->cb->on_dict_begin(s->user);
        if (st != BENCODE_OK) {
            pop_scope(s);
            return st;
        }
    }

    while (1) {
        if (at_end(s)) {
            pop_scope(s);
            return BENCODE_ERR_TRUNCATED;
        }

        /* A dict body alternates: key (must be a byte string), value, key,
         * value, ... The expecting_key flag in the scope tracks which we
         * are at. */
        sax_scope *sc = &s->scopes[s->depth - 1];

        if (peek(s) == 'e') {
            /* The dict closes only when we are at a key boundary -- i.e.
             * the previous key has its matching value. `d1:ae` lands here
             * with expecting_key == 0 and must be rejected (BEP 3
             * requires alternating string keys and values). */
            if (!sc->expecting_key) {
                pop_scope(s);
                return BENCODE_ERR_DICT_MISSING_VALUE;
            }
            s->p++;
            pop_scope(s);
            if (s->cb != NULL && s->cb->on_dict_end != NULL) {
                return s->cb->on_dict_end(s->user);
            }
            return BENCODE_OK;
        }

        if (sc->expecting_key) {
            st = parse_dict_key(s, sc);
            if (st != BENCODE_OK) {
                pop_scope(s);
                return st;
            }
        } else {
            st = parse_value(s);
            if (st != BENCODE_OK) {
                pop_scope(s);
                return st;
            }
            sc->expecting_key = 1;
        }
    }
}

/** Dispatch on the first byte of a value. */
static bencode_status parse_value(sax_state *s) {
    if (at_end(s)) {
        return BENCODE_ERR_TRUNCATED;
    }
    uint8_t b = peek(s);
    if (b == 'i') {
        return parse_int(s);
    }
    if (b == 'l') {
        return parse_list(s);
    }
    if (b == 'd') {
        return parse_dict(s);
    }
    if (is_digit(b)) {
        const uint8_t *bytes = NULL;
        size_t len = 0;
        bencode_status st = parse_string(s, &bytes, &len);
        if (st != BENCODE_OK) {
            return st;
        }
        if (s->cb != NULL && s->cb->on_string != NULL) {
            return s->cb->on_string(bytes, len, s->user);
        }
        return BENCODE_OK;
    }
    return BENCODE_ERR_UNEXPECTED_BYTE;
}

/* -- Public entry ----------------------------------------------------------- */

bencode_status bencode_parse_sax(const uint8_t *input, size_t input_size,
                                 const bencode_callbacks *callbacks, void *user, size_t max_depth,
                                 size_t *consumed) {
    if (consumed != NULL) {
        *consumed = 0;
    }
    if (input == NULL && input_size != 0) {
        return BENCODE_ERR_INVALID_ARG;
    }
    /* Zero-byte input is "no value at all" -- there's nothing to parse,
     * and constructing pointer cursors at NULL would force us into
     * NULL-arithmetic / NULL-comparison UB (the at_end() check uses >=
     * on the cursors, which is undefined when either is a null
     * pointer). Short-circuit before any pointer math. */
    if (input_size == 0) {
        return BENCODE_ERR_TRUNCATED;
    }

    /* Clamp max_depth: 0 -> default; > internal cap -> internal cap. */
    size_t effective = max_depth == 0 ? BENCODE_DEFAULT_MAX_DEPTH : max_depth;
    if (effective > BENCODE_INTERNAL_MAX_DEPTH) {
        effective = BENCODE_INTERNAL_MAX_DEPTH;
    }

    sax_state s;
    s.p = input;
    s.end = input + input_size;
    s.origin = input;
    s.cb = callbacks;
    s.user = user;
    s.depth = 0;
    s.max_depth = effective;
    /* Scopes are populated lazily by push_scope. */

    bencode_status st = parse_value(&s);
    if (consumed != NULL) {
        *consumed = (size_t)(s.p - s.origin);
    }
    return st;
}
