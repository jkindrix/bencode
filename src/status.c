/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file status.c
 * @brief Implementation of ::bencode_status_string.
 */
#include "bencode/bencode.h"

const char *bencode_status_string(bencode_status status) {
    switch (status) {
    case BENCODE_OK:
        return "ok";
    case BENCODE_ERR_INVALID_ARG:
        return "invalid argument";
    case BENCODE_ERR_TRUNCATED:
        return "input truncated mid-value";
    case BENCODE_ERR_UNEXPECTED_BYTE:
        return "unexpected byte where a value was expected";
    case BENCODE_ERR_INTEGER_OVERFLOW:
        return "integer outside int64_t range";
    case BENCODE_ERR_INTEGER_FORMAT:
        return "malformed integer (leading zero, -0, or empty)";
    case BENCODE_ERR_STRING_LENGTH:
        return "byte-string length exceeds remaining input";
    case BENCODE_ERR_DICT_UNSORTED:
        return "dictionary keys not in lexicographic order";
    case BENCODE_ERR_DICT_DUPLICATE:
        return "dictionary contains duplicate key";
    case BENCODE_ERR_DICT_BAD_KEY:
        return "dictionary key is not a byte string";
    case BENCODE_ERR_NESTING_TOO_DEEP:
        return "nesting depth exceeded limit";
    case BENCODE_ERR_NOMEM:
        return "memory allocation failed";
    case BENCODE_ERR_IO:
        return "I/O error";
    case BENCODE_ERR_USER_ABORTED:
        return "parse aborted by caller";
    default:
        return "unknown error";
    }
}
