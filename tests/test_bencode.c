/*
 * SPDX-License-Identifier: MIT
 *
 * Unit tests for libbencode. Uses a small in-file harness so the project
 * has no mandatory external test dependency.
 *
 * Each TEST() runs in isolation. CHECK logs a failure and continues;
 * REQUIRE logs and aborts the surrounding test body so a contract
 * regression produces a clean diagnostic instead of crashing the runner.
 */
/* strdup is POSIX, not C17. Must precede any system header. */
#if defined(__unix__) || defined(__APPLE__)
#ifndef _POSIX_C_SOURCE
/* NOLINTNEXTLINE(readability-identifier-naming) */
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "bencode/bencode.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_tests = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define REQUIRE(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL: %s:%d: %s (required; aborting test)\n", __FILE__, __LINE__,   \
                    #cond);                                                                        \
            ++g_failures;                                                                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define TEST(name)                                                                                 \
    static void name(void);                                                                        \
    static void run_##name(void) {                                                                 \
        ++g_tests;                                                                                 \
        fprintf(stderr, "[ RUN  ] %s\n", #name);                                                   \
        int before = g_failures;                                                                   \
        name();                                                                                    \
        fprintf(stderr, "[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", #name);             \
    }                                                                                              \
    static void name(void)

/* -- Helpers --------------------------------------------------------------- */

/** Parse a NUL-terminated literal as if it were the input; returns the
 *  status, optionally yielding the parsed tree. Closes over the trailing-
 *  byte rejection option. */
static bencode_status parse_str(const char *literal, bencode_value **out, size_t *consumed) {
    bencode_parse_options opts = {0};
    opts.reject_trailing = 1;
    return bencode_parse((const uint8_t *)literal, strlen(literal), &opts, out, consumed);
}

/** Same, but allows trailing bytes. */
static bencode_status parse_str_lenient(const char *literal, bencode_value **out,
                                        size_t *consumed) {
    return bencode_parse((const uint8_t *)literal, strlen(literal), NULL, out, consumed);
}

/* -- Integer parsing ------------------------------------------------------- */

TEST(int_zero) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("i0e", &v, NULL) == BENCODE_OK);
    bencode_int_t out = -1;
    REQUIRE(bencode_value_int(v, &out) == BENCODE_OK);
    CHECK(out == 0);
    bencode_value_free(v);
}

TEST(int_positive_simple) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("i42e", &v, NULL) == BENCODE_OK);
    bencode_int_t out = 0;
    CHECK(bencode_value_int(v, &out) == BENCODE_OK);
    CHECK(out == 42);
    bencode_value_free(v);
}

TEST(int_negative) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("i-7e", &v, NULL) == BENCODE_OK);
    bencode_int_t out = 0;
    CHECK(bencode_value_int(v, &out) == BENCODE_OK);
    CHECK(out == -7);
    bencode_value_free(v);
}

TEST(int_max_int64) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("i9223372036854775807e", &v, NULL) == BENCODE_OK);
    bencode_int_t out = 0;
    CHECK(bencode_value_int(v, &out) == BENCODE_OK);
    CHECK(out == INT64_MAX);
    bencode_value_free(v);
}

TEST(int_min_int64) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("i-9223372036854775808e", &v, NULL) == BENCODE_OK);
    bencode_int_t out = 0;
    CHECK(bencode_value_int(v, &out) == BENCODE_OK);
    CHECK(out == INT64_MIN);
    bencode_value_free(v);
}

TEST(int_positive_overflow) {
    bencode_value *v = NULL;
    /* 2^63 = 9223372036854775808 > INT64_MAX */
    CHECK(parse_str("i9223372036854775808e", &v, NULL) == BENCODE_ERR_INTEGER_OVERFLOW);
    CHECK(v == NULL);
}

TEST(int_negative_overflow) {
    bencode_value *v = NULL;
    /* -(2^63 + 1) < INT64_MIN */
    CHECK(parse_str("i-9223372036854775809e", &v, NULL) == BENCODE_ERR_INTEGER_OVERFLOW);
    CHECK(v == NULL);
}

TEST(int_leading_zero_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("i03e", &v, NULL) == BENCODE_ERR_INTEGER_FORMAT);
    CHECK(v == NULL);
}

TEST(int_negative_zero_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("i-0e", &v, NULL) == BENCODE_ERR_INTEGER_FORMAT);
    CHECK(v == NULL);
}

TEST(int_bare_minus_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("i-e", &v, NULL) == BENCODE_ERR_INTEGER_FORMAT);
    CHECK(v == NULL);
}

TEST(int_empty_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("ie", &v, NULL) == BENCODE_ERR_INTEGER_FORMAT);
    CHECK(v == NULL);
}

TEST(int_missing_terminator_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("i42", &v, NULL) == BENCODE_ERR_TRUNCATED);
    CHECK(v == NULL);
}

/* -- String parsing -------------------------------------------------------- */

TEST(string_empty) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("0:", &v, NULL) == BENCODE_OK);
    const uint8_t *bytes = NULL;
    size_t len = 99;
    CHECK(bencode_value_string(v, &bytes, &len) == BENCODE_OK);
    CHECK(len == 0);
    bencode_value_free(v);
}

TEST(string_simple) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("4:spam", &v, NULL) == BENCODE_OK);
    const uint8_t *bytes = NULL;
    size_t len = 0;
    REQUIRE(bencode_value_string(v, &bytes, &len) == BENCODE_OK);
    CHECK(len == 4);
    CHECK(memcmp(bytes, "spam", 4) == 0);
    bencode_value_free(v);
}

TEST(string_with_embedded_nul) {
    /* "ab\0c" -- four bytes including the NUL. */
    const uint8_t input[] = {'4', ':', 'a', 'b', '\0', 'c'};
    bencode_value *v = NULL;
    REQUIRE(bencode_parse(input, sizeof input, NULL, &v, NULL) == BENCODE_OK);
    const uint8_t *bytes = NULL;
    size_t len = 0;
    REQUIRE(bencode_value_string(v, &bytes, &len) == BENCODE_OK);
    CHECK(len == 4);
    CHECK(bytes[0] == 'a');
    CHECK(bytes[1] == 'b');
    CHECK(bytes[2] == '\0');
    CHECK(bytes[3] == 'c');
    bencode_value_free(v);
}

TEST(string_length_too_long_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("10:abc", &v, NULL) == BENCODE_ERR_STRING_LENGTH);
    CHECK(v == NULL);
}

TEST(string_length_leading_zero_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("04:spam", &v, NULL) == BENCODE_ERR_INTEGER_FORMAT);
    CHECK(v == NULL);
}

TEST(string_missing_colon_rejected) {
    bencode_value *v = NULL;
    /* '4spam' - the parser reads the length, then expects ':', finds 's'. */
    CHECK(parse_str("4spam", &v, NULL) == BENCODE_ERR_UNEXPECTED_BYTE);
    CHECK(v == NULL);
}

/* -- List parsing ---------------------------------------------------------- */

TEST(list_empty) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("le", &v, NULL) == BENCODE_OK);
    CHECK(bencode_value_type(v) == BENCODE_LIST);
    CHECK(bencode_list_size(v) == 0);
    CHECK(bencode_list_at(v, 0) == NULL);
    bencode_value_free(v);
}

TEST(list_mixed) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("l4:spami42ee", &v, NULL) == BENCODE_OK);
    REQUIRE(bencode_value_type(v) == BENCODE_LIST);
    REQUIRE(bencode_list_size(v) == 2);
    const bencode_value *a = bencode_list_at(v, 0);
    const bencode_value *b = bencode_list_at(v, 1);
    REQUIRE(a != NULL && b != NULL);
    CHECK(bencode_value_type(a) == BENCODE_STRING);
    CHECK(bencode_value_type(b) == BENCODE_INT);
    bencode_int_t bi = 0;
    CHECK(bencode_value_int(b, &bi) == BENCODE_OK);
    CHECK(bi == 42);
    bencode_value_free(v);
}

TEST(list_unterminated_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("l4:spam", &v, NULL) == BENCODE_ERR_TRUNCATED);
    CHECK(v == NULL);
}

/* -- Dict parsing ---------------------------------------------------------- */

TEST(dict_empty) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("de", &v, NULL) == BENCODE_OK);
    CHECK(bencode_value_type(v) == BENCODE_DICT);
    CHECK(bencode_dict_size(v) == 0);
    bencode_value_free(v);
}

TEST(dict_sorted_simple) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("d3:cow3:moo4:spam4:eggse", &v, NULL) == BENCODE_OK);
    REQUIRE(bencode_dict_size(v) == 2);
    const bencode_value *cow = bencode_dict_get(v, (const uint8_t *)"cow", 3);
    REQUIRE(cow != NULL);
    const uint8_t *bytes = NULL;
    size_t len = 0;
    REQUIRE(bencode_value_string(cow, &bytes, &len) == BENCODE_OK);
    CHECK(len == 3 && memcmp(bytes, "moo", 3) == 0);

    const bencode_value *spam = bencode_dict_get(v, (const uint8_t *)"spam", 4);
    REQUIRE(spam != NULL);
    REQUIRE(bencode_value_string(spam, &bytes, &len) == BENCODE_OK);
    CHECK(len == 4 && memcmp(bytes, "eggs", 4) == 0);

    /* Missing key */
    CHECK(bencode_dict_get(v, (const uint8_t *)"nope", 4) == NULL);
    bencode_value_free(v);
}

TEST(dict_unsorted_rejected) {
    /* "b" before "a" -- out of order. */
    bencode_value *v = NULL;
    CHECK(parse_str("d1:b0:1:a0:e", &v, NULL) == BENCODE_ERR_DICT_UNSORTED);
    CHECK(v == NULL);
}

TEST(dict_duplicate_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("d1:a0:1:a0:e", &v, NULL) == BENCODE_ERR_DICT_DUPLICATE);
    CHECK(v == NULL);
}

TEST(dict_int_key_rejected) {
    bencode_value *v = NULL;
    CHECK(parse_str("di42e3:fooe", &v, NULL) == BENCODE_ERR_DICT_BAD_KEY);
    CHECK(v == NULL);
}

TEST(dict_missing_value_rejected) {
    /* d1:ae -- key "a" but no value before the closing 'e'. BEP 3
     * requires dict bodies to alternate string keys and values, so this
     * malformed input must be rejected. */
    bencode_value *v = NULL;
    CHECK(parse_str("d1:ae", &v, NULL) == BENCODE_ERR_DICT_MISSING_VALUE);
    CHECK(v == NULL);

    /* The CLI's validate subcommand reaches the same conclusion -- not
     * tested here (that's a CLI smoke test in tests/CMakeLists.txt). */
}

TEST(dict_partial_pair_after_complete_one_rejected) {
    /* A dict that starts well-formed (a -> 1) then has a trailing key
     * with no value. The successful key+value sets expecting_key=1; the
     * next key arrives, expecting_key=0; then 'e' closes -- must reject. */
    bencode_value *v = NULL;
    CHECK(parse_str("d1:ai1e1:be", &v, NULL) == BENCODE_ERR_DICT_MISSING_VALUE);
    CHECK(v == NULL);
}

/* -- Trailing-byte handling ------------------------------------------------ */

TEST(trailing_bytes_lenient_accepted) {
    bencode_value *v = NULL;
    size_t consumed = 0;
    REQUIRE(parse_str_lenient("i42ejunk", &v, &consumed) == BENCODE_OK);
    CHECK(consumed == 4); /* "i42e" */
    bencode_value_free(v);
}

TEST(trailing_bytes_strict_rejected) {
    bencode_value *v = NULL;
    size_t consumed = 0;
    CHECK(parse_str("i42ejunk", &v, &consumed) == BENCODE_ERR_UNEXPECTED_BYTE);
    /* The strict-mode rejection happens *after* parse_sax succeeds at
     * consumed==4, then bencode_parse re-checks. We can't make a strong
     * assertion about the offset here without coupling to that detail. */
    CHECK(v == NULL);
}

/* -- Depth limit ----------------------------------------------------------- */

TEST(depth_limit_enforced) {
    /* Build a string that opens 80 nested lists and never closes -- depth
     * limit (default 64) should trip before we run out of input. */
    char buf[256];
    for (size_t i = 0; i < 80; ++i) {
        buf[i] = 'l';
    }
    buf[80] = '\0';
    bencode_value *v = NULL;
    bencode_parse_options opts = {0};
    opts.max_depth = 64;
    bencode_status st = bencode_parse((const uint8_t *)buf, 80, &opts, &v, NULL);
    CHECK(st == BENCODE_ERR_NESTING_TOO_DEEP);
    CHECK(v == NULL);
}

/* -- NULL-arg handling ----------------------------------------------------- */

TEST(parse_null_input_with_size_rejected) {
    bencode_value *v = NULL;
    CHECK(bencode_parse(NULL, 5, NULL, &v, NULL) == BENCODE_ERR_INVALID_ARG);
    CHECK(v == NULL);
}

TEST(parse_null_input_zero_size_is_truncated) {
    bencode_value *v = NULL;
    /* Zero bytes is a valid input length, but there's no value in it. */
    CHECK(bencode_parse(NULL, 0, NULL, &v, NULL) == BENCODE_ERR_TRUNCATED);
    CHECK(v == NULL);
}

TEST(parse_null_out_rejected) {
    const uint8_t input[] = "i0e";
    CHECK(bencode_parse(input, 3, NULL, NULL, NULL) == BENCODE_ERR_INVALID_ARG);
}

/* -- Builder API ----------------------------------------------------------- */

/* Test deliberately exercises many REQUIREs in sequence (each adding a
 * branch); complexity above the default threshold is fine here. */
/* NOLINTNEXTLINE(readability-function-cognitive-complexity) */
TEST(builder_dict_keeps_sorted) {
    bencode_value *d = NULL;
    REQUIRE(bencode_dict_new(NULL, &d) == BENCODE_OK);
    bencode_value *one = NULL;
    bencode_value *two = NULL;
    REQUIRE(bencode_int_new(1, NULL, &one) == BENCODE_OK);
    REQUIRE(bencode_int_new(2, NULL, &two) == BENCODE_OK);
    /* Insert out of order; expect storage in lex order. */
    REQUIRE(bencode_dict_set(d, (const uint8_t *)"b", 1, two) == BENCODE_OK);
    REQUIRE(bencode_dict_set(d, (const uint8_t *)"a", 1, one) == BENCODE_OK);

    REQUIRE(bencode_dict_size(d) == 2);
    const uint8_t *k = NULL;
    size_t klen = 0;
    const bencode_value *val = NULL;
    REQUIRE(bencode_dict_at(d, 0, &k, &klen, &val) == BENCODE_OK);
    CHECK(klen == 1 && k[0] == 'a');
    REQUIRE(bencode_dict_at(d, 1, &k, &klen, &val) == BENCODE_OK);
    CHECK(klen == 1 && k[0] == 'b');
    bencode_value_free(d);
}

TEST(builder_dict_set_replaces) {
    bencode_value *d = NULL;
    REQUIRE(bencode_dict_new(NULL, &d) == BENCODE_OK);
    bencode_value *v1 = NULL;
    bencode_value *v2 = NULL;
    REQUIRE(bencode_int_new(1, NULL, &v1) == BENCODE_OK);
    REQUIRE(bencode_int_new(99, NULL, &v2) == BENCODE_OK);
    REQUIRE(bencode_dict_set(d, (const uint8_t *)"k", 1, v1) == BENCODE_OK);
    REQUIRE(bencode_dict_set(d, (const uint8_t *)"k", 1, v2) == BENCODE_OK);
    CHECK(bencode_dict_size(d) == 1);
    const bencode_value *got = bencode_dict_get(d, (const uint8_t *)"k", 1);
    REQUIRE(got != NULL);
    bencode_int_t i = 0;
    REQUIRE(bencode_value_int(got, &i) == BENCODE_OK);
    CHECK(i == 99);
    bencode_value_free(d);
}

/* If REQUIRE bails mid-test, `input`/`parsed` may leak. That's an
 * intentional trade for test clarity: a leak in the test process when
 * a contract regression has already broken parsing is the least of the
 * caller's problems. */
/* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) */
TEST(builder_clone_detaches_strings) {
    /* Parse with borrowed string pointers, then clone, then free the
     * input buffer and confirm the cloned tree is still readable. */
    char *input = strdup("d3:cow3:mooe");
    REQUIRE(input != NULL);

    bencode_value *parsed = NULL;
    /* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) */
    REQUIRE(bencode_parse((const uint8_t *)input, strlen(input), NULL, &parsed, NULL) ==
            BENCODE_OK);

    bencode_value *clone = NULL;
    REQUIRE(bencode_value_clone(parsed, NULL, &clone) == BENCODE_OK);

    /* Free the parsed tree and its input buffer; the clone must still work. */
    bencode_value_free(parsed);
    memset(input, 0xAA, strlen(input));
    free(input);

    REQUIRE(bencode_dict_size(clone) == 1);
    const bencode_value *cow = bencode_dict_get(clone, (const uint8_t *)"cow", 3);
    REQUIRE(cow != NULL);
    const uint8_t *bytes = NULL;
    size_t len = 0;
    REQUIRE(bencode_value_string(cow, &bytes, &len) == BENCODE_OK);
    CHECK(len == 3 && memcmp(bytes, "moo", 3) == 0);
    bencode_value_free(clone);
}

TEST(partial_allocator_rejected) {
    /* Allocator with alloc set but free NULL would silently mix a
     * custom alloc with stdlib free. The public API must reject. */
    bencode_allocator partial = {0};
    partial.alloc = (void *(*)(size_t, void *))1; /* opaque non-NULL */
    partial.free = NULL;
    bencode_value *v = NULL;
    CHECK(bencode_int_new(0, &partial, &v) == BENCODE_ERR_INVALID_ARG);
    CHECK(v == NULL);

    /* And the inverse: free set, alloc NULL. */
    bencode_allocator partial2 = {0};
    partial2.alloc = NULL;
    partial2.free = (void (*)(void *, void *))1;
    CHECK(bencode_int_new(0, &partial2, &v) == BENCODE_ERR_INVALID_ARG);
    CHECK(v == NULL);
}

TEST(builder_wrong_type_accessor_returns_invalid_arg) {
    bencode_value *s = NULL;
    REQUIRE(bencode_string_new((const uint8_t *)"hi", 2, NULL, &s) == BENCODE_OK);
    bencode_int_t i = 0;
    CHECK(bencode_value_int(s, &i) == BENCODE_ERR_INVALID_ARG);
    bencode_value_free(s);
}

/* -- Emit + roundtrip property -------------------------------------------- */

typedef struct buf_sink {
    uint8_t bytes[1024];
    size_t len;
    int oom;
} buf_sink;

static bencode_status buf_sink_append(const void *bytes, size_t len, void *user) {
    buf_sink *s = user;
    if (s->len + len > sizeof s->bytes) {
        s->oom = 1;
        return BENCODE_ERR_NOMEM;
    }
    memcpy(s->bytes + s->len, bytes, len);
    s->len += len;
    return BENCODE_OK;
}

/** Decode @p input, emit through a buffer sink, and assert bit-exact
 *  reproduction. The roundtrip property is what makes Bencode canonical. */
static void assert_roundtrip(const char *literal) {
    bencode_value *v = NULL;
    bencode_status st = parse_str(literal, &v, NULL);
    if (st != BENCODE_OK) {
        fprintf(stderr, "  FAIL: parse %s: %s\n", literal, bencode_status_string(st));
        ++g_failures;
        return;
    }
    buf_sink sink = {0};
    st = bencode_emit(v, buf_sink_append, &sink);
    if (st != BENCODE_OK) {
        fprintf(stderr, "  FAIL: emit %s: %s\n", literal, bencode_status_string(st));
        ++g_failures;
        bencode_value_free(v);
        return;
    }
    size_t n = strlen(literal);
    if (sink.len != n || memcmp(sink.bytes, literal, n) != 0) {
        fprintf(stderr, "  FAIL: roundtrip mismatch for %s: got '%.*s'\n", literal, (int)sink.len,
                (char *)sink.bytes);
        ++g_failures;
    }
    bencode_value_free(v);
}

TEST(roundtrip_corpus) {
    assert_roundtrip("i0e");
    assert_roundtrip("i42e");
    assert_roundtrip("i-7e");
    assert_roundtrip("i9223372036854775807e");
    assert_roundtrip("i-9223372036854775808e");
    assert_roundtrip("0:");
    assert_roundtrip("4:spam");
    assert_roundtrip("le");
    assert_roundtrip("de");
    assert_roundtrip("l4:spami42ee");
    assert_roundtrip("d3:cow3:moo4:spaml1:a1:bee");
    assert_roundtrip("d1:ai1e1:bi2e1:ci3ee");
    assert_roundtrip("llee");   /* list containing one empty list */
    assert_roundtrip("ldee");   /* list containing one empty dict */
    assert_roundtrip("d0:lee"); /* dict { "": empty list } -- empty key */
}

TEST(emit_to_alloc_basic) {
    bencode_value *v = NULL;
    REQUIRE(parse_str("d3:cow3:moo4:spam4:eggse", &v, NULL) == BENCODE_OK);

    uint8_t *bytes = NULL;
    size_t len = 0;
    REQUIRE(bencode_emit_to_alloc(v, NULL, &bytes, &len) == BENCODE_OK);
    REQUIRE(bytes != NULL);
    CHECK(len == strlen("d3:cow3:moo4:spam4:eggse"));
    CHECK(memcmp(bytes, "d3:cow3:moo4:spam4:eggse", len) == 0);

    bencode_buffer_free(NULL, bytes);
    bencode_value_free(v);
}

TEST(emit_to_alloc_null_args_rejected) {
    bencode_value *v = NULL;
    REQUIRE(bencode_int_new(1, NULL, &v) == BENCODE_OK);
    uint8_t *bytes = NULL;
    size_t len = 0;
    CHECK(bencode_emit_to_alloc(NULL, NULL, &bytes, &len) == BENCODE_ERR_INVALID_ARG);
    CHECK(bencode_emit_to_alloc(v, NULL, NULL, &len) == BENCODE_ERR_INVALID_ARG);
    CHECK(bencode_emit_to_alloc(v, NULL, &bytes, NULL) == BENCODE_ERR_INVALID_ARG);
    bencode_value_free(v);
}

TEST(value_type_of_null_is_invalid) {
    /* NULL must map to the BENCODE_INVALID sentinel rather than any
     * real type tag. Callers using a switch with a default arm rely
     * on this. */
    CHECK(bencode_value_type(NULL) == BENCODE_INVALID);
}

TEST(emit_int_boundaries) {
    bencode_value *v = NULL;
    REQUIRE(bencode_int_new(INT64_MIN, NULL, &v) == BENCODE_OK);
    buf_sink sink = {0};
    REQUIRE(bencode_emit(v, buf_sink_append, &sink) == BENCODE_OK);
    const char *expected = "i-9223372036854775808e";
    CHECK(sink.len == strlen(expected));
    CHECK(memcmp(sink.bytes, expected, strlen(expected)) == 0);
    bencode_value_free(v);
}

/* -- Status, version ------------------------------------------------------- */

TEST(status_string_covers_all_codes) {
    /* Every defined status must produce a non-empty, non-NULL string. */
    static const bencode_status codes[] = {
        BENCODE_OK,
        BENCODE_ERR_INVALID_ARG,
        BENCODE_ERR_TRUNCATED,
        BENCODE_ERR_UNEXPECTED_BYTE,
        BENCODE_ERR_INTEGER_OVERFLOW,
        BENCODE_ERR_INTEGER_FORMAT,
        BENCODE_ERR_STRING_LENGTH,
        BENCODE_ERR_DICT_UNSORTED,
        BENCODE_ERR_DICT_DUPLICATE,
        BENCODE_ERR_DICT_BAD_KEY,
        BENCODE_ERR_NESTING_TOO_DEEP,
        BENCODE_ERR_NOMEM,
        BENCODE_ERR_IO,
        BENCODE_ERR_USER_ABORTED,
        BENCODE_ERR_DICT_MISSING_VALUE,
    };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i) {
        const char *s = bencode_status_string(codes[i]);
        if (s == NULL) {
            fprintf(stderr, "  FAIL: status_string(%d) returned NULL\n", (int)codes[i]);
            ++g_failures;
            continue;
        }
        if (s[0] == '\0') {
            fprintf(stderr, "  FAIL: status_string(%d) returned empty\n", (int)codes[i]);
            ++g_failures;
        }
    }
    /* Unknown code -- the cast is deliberate; the switch's default arm
     * must still produce a non-empty string. */
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
    const char *u = bencode_status_string((bencode_status)9999);
    CHECK(u != NULL && u[0] != '\0');
}

TEST(version_is_nonempty) {
    const char *v = bencode_version();
    REQUIRE(v != NULL);
    CHECK(v[0] != '\0');
}

/* -- SAX-level callback abort -------------------------------------------- */

typedef struct abort_ctx {
    int seen_ints;
    bencode_status abort_after_n_ints;
} abort_ctx;

static bencode_status abort_on_int(bencode_int_t value, void *user) {
    (void)value;
    abort_ctx *c = user;
    c->seen_ints++;
    if (c->seen_ints >= 2) {
        return c->abort_after_n_ints;
    }
    return BENCODE_OK;
}

TEST(sax_callback_abort_propagates) {
    abort_ctx c = {0, BENCODE_ERR_USER_ABORTED};
    bencode_callbacks cb = {0};
    cb.on_int = abort_on_int;
    const char *input = "li1ei2ei3ee";
    bencode_status st = bencode_parse_sax((const uint8_t *)input, strlen(input), &cb, &c, 0, NULL);
    CHECK(st == BENCODE_ERR_USER_ABORTED);
    CHECK(c.seen_ints == 2);
}

/* -- Test runner ----------------------------------------------------------- */

int main(void) {
    run_int_zero();
    run_int_positive_simple();
    run_int_negative();
    run_int_max_int64();
    run_int_min_int64();
    run_int_positive_overflow();
    run_int_negative_overflow();
    run_int_leading_zero_rejected();
    run_int_negative_zero_rejected();
    run_int_bare_minus_rejected();
    run_int_empty_rejected();
    run_int_missing_terminator_rejected();

    run_string_empty();
    run_string_simple();
    run_string_with_embedded_nul();
    run_string_length_too_long_rejected();
    run_string_length_leading_zero_rejected();
    run_string_missing_colon_rejected();

    run_list_empty();
    run_list_mixed();
    run_list_unterminated_rejected();

    run_dict_empty();
    run_dict_sorted_simple();
    run_dict_unsorted_rejected();
    run_dict_duplicate_rejected();
    run_dict_int_key_rejected();
    run_dict_missing_value_rejected();
    run_dict_partial_pair_after_complete_one_rejected();

    run_trailing_bytes_lenient_accepted();
    run_trailing_bytes_strict_rejected();

    run_depth_limit_enforced();

    run_parse_null_input_with_size_rejected();
    run_parse_null_input_zero_size_is_truncated();
    run_parse_null_out_rejected();

    run_builder_dict_keeps_sorted();
    run_builder_dict_set_replaces();
    run_builder_clone_detaches_strings();
    run_partial_allocator_rejected();
    run_builder_wrong_type_accessor_returns_invalid_arg();

    run_roundtrip_corpus();
    run_emit_to_alloc_basic();
    run_emit_to_alloc_null_args_rejected();
    run_value_type_of_null_is_invalid();
    run_emit_int_boundaries();

    run_status_string_covers_all_codes();
    run_version_is_nonempty();

    run_sax_callback_abort_propagates();

    fprintf(stderr, "\n%d test(s), %d failure(s)\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
