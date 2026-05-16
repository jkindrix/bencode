/*
 * SPDX-License-Identifier: MIT
 *
 * Symbol-visibility macros and small attribute helpers. Public declarations
 * carry BENCODE_API so shared-library builds can hide everything else with
 * -fvisibility=hidden. The attribute helpers (BENCODE_NODISCARD,
 * BENCODE_NORETURN, BENCODE_PRINTF) are also exposed here because callers
 * benefit from the compile-time enforcement they provide.
 */
#ifndef BENCODE_EXPORT_H
#define BENCODE_EXPORT_H

/* -- BENCODE_API: public-symbol visibility ---------------------------------- */
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(BENCODE_BUILD_SHARED)
#define BENCODE_API __declspec(dllexport)
#elif defined(BENCODE_USE_SHARED)
#define BENCODE_API __declspec(dllimport)
#else
#define BENCODE_API
#endif
#else
#if defined(BENCODE_BUILD_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define BENCODE_API __attribute__((visibility("default")))
#else
#define BENCODE_API
#endif
#endif

/* -- BENCODE_NODISCARD: warn if the return value is ignored ----------------- */
#if defined(__GNUC__) || defined(__clang__)
#define BENCODE_NODISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#define BENCODE_NODISCARD _Check_return_
#else
#define BENCODE_NODISCARD
#endif

/* -- BENCODE_NORETURN: function doesn't return ------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
#define BENCODE_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define BENCODE_NORETURN __declspec(noreturn)
#else
#define BENCODE_NORETURN
#endif

/* -- BENCODE_PRINTF: type-check printf-style args --------------------------- */
#if defined(__GNUC__) || defined(__clang__)
#define BENCODE_PRINTF(fmt_idx, vararg_idx) __attribute__((format(printf, fmt_idx, vararg_idx)))
#else
#define BENCODE_PRINTF(fmt_idx, vararg_idx)
#endif

#endif /* BENCODE_EXPORT_H */
