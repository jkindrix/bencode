/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 *
 * Public API for the bencode library: a strict Bencode (BitTorrent encoding)
 * parser, emitter, and value tree.
 *
 * Bencode (https://wiki.theory.org/BitTorrentSpecification#Bencoding) has
 * four data types:
 *
 *     Integer:    i<base-10>e         e.g. i42e   i-7e   i0e
 *     Byte string: <length>:<bytes>   e.g. 4:spam  0:
 *     List:       l<contents>e        e.g. l4:spami42ee
 *     Dictionary: d<key1><value1>...e e.g. d3:foo3:bare
 *
 * The encoding is canonical: integers have no leading zeros (and -0 is
 * forbidden), dictionary keys are byte strings in lexicographic order, and
 * keys must be unique within a dict. The library decodes only strictly
 * canonical input and emits only strictly canonical output, so the
 * decode -> emit pipeline is bit-exact.
 *
 * The library has three layers:
 *
 *     1. SAX-style streaming parser  (bencode_parse_sax)
 *        Lowest-level. Calls back into a caller table as values are
 *        recognized. No allocation. The DOM parser is built on top.
 *
 *     2. DOM parser + value tree     (bencode_parse, bencode_value_*)
 *        Builds an in-memory tree. Byte-string payloads in the tree
 *        *borrow* pointers into the original input buffer; the input must
 *        outlive the tree. Use bencode_value_clone() if you need an owned
 *        copy.
 *
 *     3. Emitter                     (bencode_emit, bencode_emit_to_file)
 *        Writes a value tree as canonical Bencode through a caller-supplied
 *        callback (or directly to a FILE*).
 *
 * @par Thread safety: MT-Safe.
 *      The library has no global state. Every function is reentrant and
 *      may be called concurrently from multiple threads as long as the
 *      arguments themselves don't alias destructively. Specifically:
 *
 *        - Two threads may parse two different inputs in parallel.
 *        - Two threads may read (accessors) the same const bencode_value
 *          in parallel.
 *        - A mutator (bencode_list_append, bencode_dict_set) on a
 *          bencode_value is *not* internally synchronized; caller must
 *          serialize mutations to the same value.
 *
 * @par Integer width:
 *      Bencode permits arbitrary-precision integers; this library accepts
 *      values in the range of @c int64_t. Out-of-range integers are
 *      rejected with ::BENCODE_ERR_INTEGER_OVERFLOW.
 *
 * @par Allocation:
 *      All functions that allocate accept an optional
 *      ::bencode_allocator. A NULL allocator means @c malloc / @c free.
 */
#ifndef BENCODE_BENCODE_H
#define BENCODE_BENCODE_H

#include "bencode/export.h"
#include "bencode/version.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- Types ------------------------------------------------------------------ */

/** Integer type for Bencode values. The library accepts the full range. */
typedef int64_t bencode_int_t;

/** Error and status codes returned by every fallible API entry point. */
typedef enum bencode_status {
    /** Success. */
    BENCODE_OK = 0,

    /** A required argument was NULL or otherwise invalid. */
    BENCODE_ERR_INVALID_ARG = 1,

    /** Input ended in the middle of a value (e.g. `i42` with no `e`). */
    BENCODE_ERR_TRUNCATED = 2,

    /** Input contained a byte where no value type starts (not i / l / d
     *  / 0-9 / ':'). */
    BENCODE_ERR_UNEXPECTED_BYTE = 3,

    /** Integer value is outside the range of @c int64_t. */
    BENCODE_ERR_INTEGER_OVERFLOW = 4,

    /** Integer is malformed: empty body (`ie`), leading zero (`i03e`),
     *  bare minus (`i-e`), or negative zero (`i-0e`). */
    BENCODE_ERR_INTEGER_FORMAT = 5,

    /** Byte-string length prefix declares more bytes than the input has. */
    BENCODE_ERR_STRING_LENGTH = 6,

    /** Dictionary keys are not in lexicographic byte order. */
    BENCODE_ERR_DICT_UNSORTED = 7,

    /** Dictionary contains the same key twice. */
    BENCODE_ERR_DICT_DUPLICATE = 8,

    /** Dictionary key isn't a byte string (e.g. an integer key). */
    BENCODE_ERR_DICT_BAD_KEY = 9,

    /** Nesting depth exceeded the configured limit (DoS guard). */
    BENCODE_ERR_NESTING_TOO_DEEP = 10,

    /** Allocator returned NULL. */
    BENCODE_ERR_NOMEM = 11,

    /** Emit callback (or underlying FILE*) reported a write error. */
    BENCODE_ERR_IO = 12,

    /** A SAX callback aborted the parse by returning non-OK. The
     *  callback's status is propagated; this code is reserved for the
     *  parser's own use. */
    BENCODE_ERR_USER_ABORTED = 13
} bencode_status;

/** Type tag for ::bencode_value.
 *
 * ::BENCODE_INVALID is the type returned by ::bencode_value_type when
 * called with a NULL pointer. No real value will ever have this type,
 * so callers can dispatch on a switch without a separate NULL check
 * and a `default:` arm will handle the bad-pointer case.
 */
typedef enum bencode_type {
    BENCODE_INVALID = 0,
    BENCODE_INT = 1,
    BENCODE_STRING = 2,
    BENCODE_LIST = 3,
    BENCODE_DICT = 4
} bencode_type;

/** Opaque value-tree node. Construct with the builder API or
 *  ::bencode_parse; destroy with ::bencode_value_free. */
typedef struct bencode_value bencode_value;

/**
 * Caller-supplied allocator. Pass NULL to any function that takes a
 * `const bencode_allocator *` to use the platform `malloc` / `free`.
 *
 * The library only calls @p alloc and @p free; it does not call
 * realloc, calloc, or memalign.
 *
 * @par Thread safety: the caller's @p alloc / @p free functions must
 *      themselves be MT-Safe if the @ref bencode_allocator is shared
 *      across threads.
 */
typedef struct bencode_allocator {
    /** Allocate @p size bytes. Returns NULL on failure. */
    void *(*alloc)(size_t size, void *user);
    /** Free a pointer previously returned by @p alloc. NULL is a no-op. */
    void (*free)(void *ptr, void *user);
    /** Opaque user data passed verbatim to @p alloc and @p free. */
    void *user;
} bencode_allocator;

/** Default value for ::bencode_parse_options::max_depth. */
#define BENCODE_DEFAULT_MAX_DEPTH 64

/**
 * Options controlling a parse. Zero-initialize to use defaults; pass NULL
 * to ::bencode_parse for "all defaults."
 */
typedef struct bencode_parse_options {
    /** Maximum nesting depth of lists and dicts. 0 means use
     *  ::BENCODE_DEFAULT_MAX_DEPTH. Inputs deeper than the limit fail
     *  with ::BENCODE_ERR_NESTING_TOO_DEEP -- a DoS guard against
     *  malicious deeply-nested inputs. */
    size_t max_depth;

    /** Allocator for the value tree. NULL uses stdlib malloc/free. */
    const bencode_allocator *allocator;

    /** If true, ::bencode_parse rejects input that has trailing bytes
     *  after the first complete value. If false (the default), trailing
     *  bytes are allowed and the count of consumed bytes is reported via
     *  the @p consumed out-parameter. */
    int reject_trailing;
} bencode_parse_options;

/* -- SAX parser ------------------------------------------------------------- */

/**
 * Caller-supplied callbacks for the SAX parser. Any field may be NULL,
 * in which case the parser skips emitting that event but still validates
 * the structure.
 *
 * Each callback returns a ::bencode_status. Returning ::BENCODE_OK
 * continues parsing; returning anything else aborts the parse and
 * propagates that status out of ::bencode_parse_sax.
 *
 * String pointers passed to @p on_string and dictionary-key argument
 * positions point into the parser's input buffer, not into allocated
 * storage. They are valid only for the duration of the callback unless
 * the caller copies them.
 */
typedef struct bencode_callbacks {
    bencode_status (*on_int)(bencode_int_t value, void *user);
    bencode_status (*on_string)(const uint8_t *bytes, size_t len, void *user);
    bencode_status (*on_list_begin)(void *user);
    bencode_status (*on_list_end)(void *user);
    bencode_status (*on_dict_begin)(void *user);
    bencode_status (*on_dict_end)(void *user);
} bencode_callbacks;

/**
 * Parse @p input as a single Bencode value, invoking the supplied
 * callbacks for each value the parser recognizes.
 *
 * @param input       Bencode bytes. May be NULL iff @p input_size is 0.
 * @param input_size  Length of @p input in bytes.
 * @param callbacks   Callback table. May be NULL to validate without
 *                    invoking callbacks.
 * @param user        Opaque pointer passed verbatim to each callback.
 * @param max_depth   Maximum nesting depth. 0 means
 *                    ::BENCODE_DEFAULT_MAX_DEPTH.
 * @param[out] consumed
 *                    If non-NULL, receives the number of input bytes
 *                    consumed by the first complete value on success,
 *                    or the byte offset of the error on failure.
 * @return ::BENCODE_OK on success, or an error code.
 *
 * @par Thread safety: MT-Safe. Two threads may parse disjoint inputs in
 *      parallel.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_parse_sax(const uint8_t *input,
                                                               size_t input_size,
                                                               const bencode_callbacks *callbacks,
                                                               void *user, size_t max_depth,
                                                               size_t *consumed);

/* -- DOM parser ------------------------------------------------------------- */

/**
 * Parse @p input as a single Bencode value, returning a value tree.
 *
 * Byte-string nodes in the returned tree contain pointers into @p input,
 * not allocated copies. The caller must keep @p input alive at least as
 * long as the returned tree, or pre-clone with ::bencode_value_clone.
 *
 * @param input       Bencode bytes. May be NULL iff @p input_size is 0.
 * @param input_size  Length of @p input in bytes.
 * @param opts        Parse options. NULL means "all defaults."
 * @param[out] out    Receives the parsed value tree on success. Caller
 *                    owns it and must free with ::bencode_value_free.
 *                    Set to NULL on error.
 * @param[out] consumed
 *                    Number of input bytes consumed on success, or the
 *                    byte offset of the error on failure. May be NULL.
 *
 * @return ::BENCODE_OK on success, or an error code.
 *
 * @par Thread safety: MT-Safe.
 *
 * @par Stack cost:
 *      The DOM parser keeps a fixed-size scope stack of ~6 KB on the
 *      calling thread's C stack (one ~24-byte frame per possible
 *      depth level, sized to the internal cap of 257). On exotic
 *      targets with restricted thread stacks this may matter; on
 *      typical desktop/server platforms it is negligible.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_parse(const uint8_t *input, size_t input_size,
                                                           const bencode_parse_options *opts,
                                                           bencode_value **out, size_t *consumed);

/* -- DOM lifecycle ---------------------------------------------------------- */

/** Free a value tree. Recursively frees children. NULL is a no-op. */
BENCODE_API void bencode_value_free(bencode_value *value);

/**
 * Deep-clone a value tree, allocating fresh copies of all byte-string
 * payloads. Useful after ::bencode_parse to detach the tree from its
 * source input buffer.
 *
 * @param value Source value. NULL produces a NULL clone with ::BENCODE_OK.
 * @param alloc Allocator for the clone. NULL uses malloc/free.
 * @param[out] out Receives the cloned value tree.
 * @return ::BENCODE_OK on success, ::BENCODE_ERR_NOMEM on allocation
 *         failure.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_value_clone(const bencode_value *value,
                                                                 const bencode_allocator *alloc,
                                                                 bencode_value **out);

/* -- DOM accessors ---------------------------------------------------------- */

/** Type tag of @p value. Asserts internally that @p value is non-NULL. */
BENCODE_API bencode_type bencode_value_type(const bencode_value *value);

/**
 * Read an integer value.
 * @return ::BENCODE_OK if @p value is an integer (and writes it to
 *         @p out_int); ::BENCODE_ERR_INVALID_ARG otherwise.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_value_int(const bencode_value *value,
                                                               bencode_int_t *out_int);

/**
 * Read a byte-string value. The returned pointer aliases the value's
 * own storage and is valid until the value is freed.
 *
 * @return ::BENCODE_OK if @p value is a byte string; otherwise
 *         ::BENCODE_ERR_INVALID_ARG.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_value_string(const bencode_value *value,
                                                                  const uint8_t **out_bytes,
                                                                  size_t *out_len);

/** Number of elements in a list. 0 if @p value isn't a list. */
BENCODE_API size_t bencode_list_size(const bencode_value *value);

/**
 * Element at position @p index in a list (0-based). Returns NULL if
 * @p value isn't a list or @p index is out of range. The returned pointer
 * is owned by the parent and must not be freed independently.
 */
BENCODE_API const bencode_value *bencode_list_at(const bencode_value *value, size_t index);

/** Number of entries in a dictionary. 0 if @p value isn't a dict. */
BENCODE_API size_t bencode_dict_size(const bencode_value *value);

/**
 * Look up a key in a dict by exact byte-string match.
 * @return The associated value, or NULL if not found / not a dict.
 *         Borrowed pointer; do not free.
 */
BENCODE_API const bencode_value *bencode_dict_get(const bencode_value *value, const uint8_t *key,
                                                  size_t key_len);

/**
 * Iterate dictionary entries by index (entries are stored in
 * lexicographic key order).
 *
 * @param value             The dictionary value to read from.
 * @param index             Zero-based entry index.
 * @param[out] out_key      Receives a pointer to the key bytes.
 * @param[out] out_key_len  Receives the key's length.
 * @param[out] out_value    Receives the entry's value.
 *
 * @return ::BENCODE_OK if @p value is a dict and @p index is in range;
 *         ::BENCODE_ERR_INVALID_ARG otherwise.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_dict_at(const bencode_value *value,
                                                             size_t index, const uint8_t **out_key,
                                                             size_t *out_key_len,
                                                             const bencode_value **out_value);

/* -- DOM builder ------------------------------------------------------------ */

/** Construct an integer value. */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_int_new(bencode_int_t v,
                                                             const bencode_allocator *alloc,
                                                             bencode_value **out);

/**
 * Construct a byte-string value. The bytes are *copied* into freshly
 * allocated storage owned by the returned value.
 *
 * @p bytes may be NULL iff @p len is 0.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_string_new(const uint8_t *bytes, size_t len,
                                                                const bencode_allocator *alloc,
                                                                bencode_value **out);

/** Construct an empty list value. */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_list_new(const bencode_allocator *alloc,
                                                              bencode_value **out);

/**
 * Append @p child to @p list. On success, @p list takes ownership of
 * @p child; the caller must not free @p child separately.
 *
 * On failure, @p child remains owned by the caller.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_list_append(bencode_value *list,
                                                                 bencode_value *child);

/** Construct an empty dictionary value. */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_dict_new(const bencode_allocator *alloc,
                                                              bencode_value **out);

/**
 * Insert (or replace) @p child under @p key in @p dict. The key bytes
 * are copied; @p dict takes ownership of @p child on success.
 *
 * If a value with the same key already exists, it is freed and replaced.
 * Entries are maintained in lexicographic key order; emit produces them
 * in that order, matching the Bencode canonical form.
 *
 * On failure, @p child remains owned by the caller.
 *
 * @par Complexity:
 *      O(log n) search + O(n) memmove for the ordered insert, where
 *      n is the current dict size. Bulk-constructing a large dict via
 *      repeated ::bencode_dict_set is therefore O(n²); if input
 *      arrives already sorted (e.g., parsing from canonical Bencode),
 *      ::bencode_parse is the cheaper path.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_dict_set(bencode_value *dict,
                                                              const uint8_t *key, size_t key_len,
                                                              bencode_value *child);

/* -- Emitter ---------------------------------------------------------------- */

/**
 * Caller-supplied byte sink for ::bencode_emit. The library guarantees
 * @p len > 0 and @p bytes != NULL on every call.
 *
 * Returning ::BENCODE_OK indicates all @p len bytes were accepted;
 * any other return propagates out of ::bencode_emit and aborts the
 * emit. Typical implementations forward to fwrite, send, or a buffer
 * append.
 */
typedef bencode_status (*bencode_emit_fn)(const void *bytes, size_t len, void *user);

/**
 * Emit @p value as canonical Bencode through @p emit.
 *
 * The output is bit-identical to the bytes ::bencode_parse would accept
 * to reconstruct @p value (modulo trailing input).
 *
 * @par Thread safety: MT-Safe-per-instance. Concurrent emit of two
 *      different values is safe; concurrent emit of the same value
 *      is also safe (emit is read-only on the value), but the @p emit
 *      callback must itself tolerate concurrent invocation if shared.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_emit(const bencode_value *value,
                                                          bencode_emit_fn emit, void *user);

/** Convenience: emit to a stdio @c FILE*. Returns ::BENCODE_ERR_IO if a
 *  write or final fflush fails. */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_emit_to_file(const bencode_value *value,
                                                                  FILE *out);

/**
 * Convenience: emit @p value into a freshly-allocated byte buffer.
 *
 * The output buffer is allocated with @p alloc (or @c malloc when
 * @p alloc is NULL). On success the caller owns the buffer and must
 * release it with ::bencode_buffer_free, passing the same @p alloc.
 *
 * @param value      Value to emit. Must not be NULL.
 * @param alloc      Allocator. NULL uses platform @c malloc / @c free.
 * @param[out] out_bytes  Receives the buffer pointer on success.
 * @param[out] out_len    Receives the buffer length on success.
 *
 * @return ::BENCODE_OK on success, ::BENCODE_ERR_INVALID_ARG on NULL
 *         arguments, ::BENCODE_ERR_NOMEM on allocation failure.
 *
 * @par Thread safety: MT-Safe.
 */
BENCODE_API BENCODE_NODISCARD bencode_status bencode_emit_to_alloc(const bencode_value *value,
                                                                   const bencode_allocator *alloc,
                                                                   uint8_t **out_bytes,
                                                                   size_t *out_len);

/**
 * Free a buffer previously returned by ::bencode_emit_to_alloc.
 *
 * @param alloc  Allocator that was passed to ::bencode_emit_to_alloc.
 *               Must match; mixing allocators here is undefined.
 * @param bytes  Buffer pointer. NULL is a no-op.
 */
BENCODE_API void bencode_buffer_free(const bencode_allocator *alloc, uint8_t *bytes);

/* -- Misc ------------------------------------------------------------------- */

/** Library version (e.g. "0.1.0"). Never NULL. */
BENCODE_API const char *bencode_version(void);

/** Human-readable description of a status code. Never NULL, even for
 *  unknown codes. */
BENCODE_API const char *bencode_status_string(bencode_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BENCODE_BENCODE_H */
