/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file main.c
 * @brief `bencode` CLI: validate, print, roundtrip, and size subcommands.
 *
 * Usage:
 *   bencode validate  [FILE]    Validate that FILE (or stdin) is canonical Bencode.
 *   bencode print     [FILE]    Decode and print as a human-readable rendering.
 *   bencode roundtrip [FILE]    Decode, re-encode, compare bit-exact to input.
 *   bencode size      [FILE]    Print structural stats about FILE.
 *   bencode --help | -h
 *   bencode --version | -V
 *
 * FILE may be `-` (or omitted) to read from stdin.
 *
 * Exit codes (stable for shell scripting):
 *   0  success
 *   1  the input is not valid canonical Bencode, or roundtrip differs
 *   2  I/O error reading the input or writing to stdout, or an unknown
 *      option / unknown subcommand on the command line
 */
#include "bencode/bencode.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- I/O helpers ----------------------------------------------------------- */

/** Double @p *buf's capacity. Returns 0 on success, non-zero on overflow
 *  or allocation failure; on failure @p *buf is freed and set to NULL. */
static int grow_buf(uint8_t **buf, size_t *cap) {
    if (*cap > SIZE_MAX / 2U) {
        free(*buf);
        *buf = NULL;
        return 1;
    }
    size_t new_cap = *cap * 2U;
    uint8_t *grown = realloc(*buf, new_cap);
    if (grown == NULL) {
        free(*buf);
        *buf = NULL;
        return 1;
    }
    *buf = grown;
    *cap = new_cap;
    return 0;
}

/** Open @p path for reading (or return stdin if @p path is NULL / "-"). */
static FILE *open_input(const char *path, int *from_stdin) {
    *from_stdin = (path == NULL || strcmp(path, "-") == 0);
    return *from_stdin ? stdin : fopen(path, "rb");
}

/** Read the entire content of @p path (or stdin if @p path == "-") into a
 *  freshly malloc'd buffer. Returns 0 on success, non-zero on I/O error.
 *  Caller frees @p *out_buf. */
static int read_all(const char *path, uint8_t **out_buf, size_t *out_size) {
    int from_stdin = 0;
    FILE *fp = open_input(path, &from_stdin);
    if (fp == NULL) {
        return 1;
    }

    size_t cap = 4096;
    size_t len = 0;
    uint8_t *buf = malloc(cap);
    int rc = (buf == NULL) ? 1 : 0;

    while (rc == 0) {
        if (len == cap && grow_buf(&buf, &cap) != 0) {
            rc = 1;
            break;
        }
        size_t got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) {
            if (ferror(fp)) {
                free(buf);
                buf = NULL;
                rc = 1;
            }
            break;
        }
    }

    if (!from_stdin) {
        fclose(fp);
    }
    if (rc != 0) {
        return rc;
    }
    *out_buf = buf;
    *out_size = len;
    return 0;
}

/* -- Pretty printer -------------------------------------------------------- */

static int print_string(const uint8_t *bytes, size_t len, FILE *out) {
    if (fputc('"', out) == EOF) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = bytes[i];
        if (b == '"' || b == '\\') {
            if (fprintf(out, "\\%c", b) < 0) {
                return -1;
            }
        } else if (b >= 0x20 && b < 0x7f) {
            if (fputc((int)b, out) == EOF) {
                return -1;
            }
        } else {
            if (fprintf(out, "\\x%02x", b) < 0) {
                return -1;
            }
        }
    }
    if (fputc('"', out) == EOF) {
        return -1;
    }
    return 0;
}

static int print_indent(FILE *out, size_t depth) {
    for (size_t i = 0; i < depth; ++i) {
        if (fputs("  ", out) == EOF) {
            return -1;
        }
    }
    return 0;
}

static int print_value(const bencode_value *v, FILE *out, size_t depth);

static int print_list(const bencode_value *v, FILE *out, size_t depth) {
    size_t n = bencode_list_size(v);
    if (n == 0) {
        return fputs("[]", out) == EOF ? -1 : 0;
    }
    if (fputs("[\n", out) == EOF) {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (print_indent(out, depth + 1) != 0) {
            return -1;
        }
        if (print_value(bencode_list_at(v, i), out, depth + 1) != 0) {
            return -1;
        }
        if (fputs(i + 1 < n ? ",\n" : "\n", out) == EOF) {
            return -1;
        }
    }
    if (print_indent(out, depth) != 0 || fputc(']', out) == EOF) {
        return -1;
    }
    return 0;
}

static int print_dict(const bencode_value *v, FILE *out, size_t depth) {
    size_t n = bencode_dict_size(v);
    if (n == 0) {
        return fputs("{}", out) == EOF ? -1 : 0;
    }
    if (fputs("{\n", out) == EOF) {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        const uint8_t *k = NULL;
        size_t klen = 0;
        const bencode_value *child = NULL;
        if (bencode_dict_at(v, i, &k, &klen, &child) != BENCODE_OK) {
            return -1;
        }
        if (print_indent(out, depth + 1) != 0) {
            return -1;
        }
        if (print_string(k, klen, out) != 0) {
            return -1;
        }
        if (fputs(": ", out) == EOF) {
            return -1;
        }
        if (print_value(child, out, depth + 1) != 0) {
            return -1;
        }
        if (fputs(i + 1 < n ? ",\n" : "\n", out) == EOF) {
            return -1;
        }
    }
    if (print_indent(out, depth) != 0 || fputc('}', out) == EOF) {
        return -1;
    }
    return 0;
}

static int print_value(const bencode_value *v, FILE *out, size_t depth) {
    switch (bencode_value_type(v)) {
    case BENCODE_INT: {
        bencode_int_t i = 0;
        if (bencode_value_int(v, &i) != BENCODE_OK) {
            return -1;
        }
        return fprintf(out, "%lld", (long long)i) < 0 ? -1 : 0;
    }
    case BENCODE_STRING: {
        const uint8_t *bytes = NULL;
        size_t len = 0;
        if (bencode_value_string(v, &bytes, &len) != BENCODE_OK) {
            return -1;
        }
        return print_string(bytes, len, out);
    }
    case BENCODE_LIST:
        return print_list(v, out, depth);
    case BENCODE_DICT:
        return print_dict(v, out, depth);
    case BENCODE_INVALID:
    default:
        return -1;
    }
}

/* -- Subcommands ----------------------------------------------------------- */

/** Map a library status to a CLI exit code per the contract in this file's
 *  header. Currently only BENCODE_ERR_IO maps to 2; everything else non-OK
 *  is a data-level problem and maps to 1. */
static int status_to_exit(bencode_status st) {
    if (st == BENCODE_OK) {
        return 0;
    }
    if (st == BENCODE_ERR_IO) {
        return 2;
    }
    return 1;
}

/** Common preamble: read the input file and parse to a tree. Returns the
 *  CLI exit code; on success, @p *out_value is set and the caller must
 *  free it (and @p *out_buf). On failure, both are NULL and a diagnostic
 *  has been printed to stderr. */
static int load(const char *prog, const char *path, uint8_t **out_buf, size_t *out_size,
                bencode_value **out_value) {
    *out_buf = NULL;
    *out_size = 0;
    *out_value = NULL;

    if (read_all(path, out_buf, out_size) != 0) {
        fprintf(stderr, "%s: cannot read %s\n", prog,
                path == NULL || strcmp(path, "-") == 0 ? "<stdin>" : path);
        return 2;
    }

    bencode_parse_options opts = {0};
    opts.reject_trailing = 1;
    size_t consumed = 0;
    bencode_status st = bencode_parse(*out_buf, *out_size, &opts, out_value, &consumed);
    if (st != BENCODE_OK) {
        fprintf(stderr, "%s: %s at byte offset %zu\n", prog, bencode_status_string(st), consumed);
        free(*out_buf);
        *out_buf = NULL;
        return status_to_exit(st);
    }
    return 0;
}

static int cmd_validate(const char *prog, const char *path) {
    uint8_t *buf = NULL;
    size_t size = 0;
    bencode_value *value = NULL;
    int rc = load(prog, path, &buf, &size, &value);
    if (rc != 0) {
        return rc;
    }
    if (puts("ok") == EOF) {
        rc = 2;
    }
    bencode_value_free(value);
    free(buf);
    return rc;
}

static int cmd_print(const char *prog, const char *path) {
    uint8_t *buf = NULL;
    size_t size = 0;
    bencode_value *value = NULL;
    int rc = load(prog, path, &buf, &size, &value);
    if (rc != 0) {
        return rc;
    }
    if (print_value(value, stdout, 0) != 0 || fputc('\n', stdout) == EOF || fflush(stdout) != 0) {
        rc = 2;
    }
    bencode_value_free(value);
    free(buf);
    return rc;
}

static int cmd_roundtrip(const char *prog, const char *path) {
    uint8_t *buf = NULL;
    size_t size = 0;
    bencode_value *value = NULL;
    int rc = load(prog, path, &buf, &size, &value);
    if (rc != 0) {
        return rc;
    }

    uint8_t *emitted = NULL;
    size_t emitted_len = 0;
    bencode_status st = bencode_emit_to_alloc(value, NULL, &emitted, &emitted_len);
    if (st != BENCODE_OK) {
        fprintf(stderr, "%s: emit failed: %s\n", prog, bencode_status_string(st));
        bencode_value_free(value);
        free(buf);
        return status_to_exit(st);
    }

    int ok = (emitted_len == size) && (size == 0 || memcmp(emitted, buf, size) == 0);
    if (!ok) {
        fprintf(stderr, "%s: roundtrip mismatch: input %zu bytes, output %zu bytes\n", prog, size,
                emitted_len);
        rc = 1;
    } else if (puts("ok") == EOF) {
        rc = 2;
    }
    bencode_buffer_free(NULL, emitted);
    bencode_value_free(value);
    free(buf);
    return rc;
}

/* -- size subcommand: walk and tally --------------------------------------- */

typedef struct stats {
    size_t total_values;
    size_t ints;
    size_t strings;
    size_t lists;
    size_t dicts;
    size_t max_depth_seen;
    size_t total_string_bytes;
} stats;

static void walk(const bencode_value *v, stats *st, size_t depth) {
    if (v == NULL) {
        return;
    }
    st->total_values++;
    if (depth > st->max_depth_seen) {
        st->max_depth_seen = depth;
    }
    switch (bencode_value_type(v)) {
    case BENCODE_INT:
        st->ints++;
        break;
    case BENCODE_STRING: {
        st->strings++;
        const uint8_t *bytes = NULL;
        size_t len = 0;
        if (bencode_value_string(v, &bytes, &len) == BENCODE_OK) {
            st->total_string_bytes += len;
        }
        break;
    }
    case BENCODE_LIST: {
        st->lists++;
        size_t n = bencode_list_size(v);
        for (size_t i = 0; i < n; ++i) {
            walk(bencode_list_at(v, i), st, depth + 1);
        }
        break;
    }
    case BENCODE_DICT: {
        st->dicts++;
        size_t n = bencode_dict_size(v);
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *k = NULL;
            size_t klen = 0;
            const bencode_value *child = NULL;
            if (bencode_dict_at(v, i, &k, &klen, &child) == BENCODE_OK) {
                /* Keys count as string bytes too. */
                st->strings++;
                st->total_string_bytes += klen;
                walk(child, st, depth + 1);
            }
        }
        break;
    }
    case BENCODE_INVALID:
    default:
        break;
    }
}

static int cmd_size(const char *prog, const char *path) {
    uint8_t *buf = NULL;
    size_t size = 0;
    bencode_value *value = NULL;
    int rc = load(prog, path, &buf, &size, &value);
    if (rc != 0) {
        return rc;
    }
    stats st = {0};
    walk(value, &st, 0);
    if (printf("input_bytes:        %zu\n"
               "total_values:       %zu\n"
               "  integers:         %zu\n"
               "  strings:          %zu\n"
               "  lists:            %zu\n"
               "  dicts:            %zu\n"
               "max_depth_seen:     %zu\n"
               "total_string_bytes: %zu\n",
               size, st.total_values, st.ints, st.strings, st.lists, st.dicts, st.max_depth_seen,
               st.total_string_bytes) < 0) {
        rc = 2;
    }
    bencode_value_free(value);
    free(buf);
    return rc;
}

/* -- Usage / dispatch ------------------------------------------------------ */

static int print_usage(FILE *stream, const char *prog) {
    int rc = fprintf(stream,
                     "Usage: %s <subcommand> [FILE]\n"
                     "       %s {--help | -h}\n"
                     "       %s {--version | -V}\n"
                     "\n"
                     "Subcommands:\n"
                     "  validate   Verify that FILE contains canonical Bencode.\n"
                     "  print      Decode and pretty-print FILE in a human-readable form.\n"
                     "  roundtrip  Decode then re-encode; assert bit-exact match.\n"
                     "  size       Print structural statistics about FILE.\n"
                     "\n"
                     "FILE may be `-` or omitted to read from standard input.\n"
                     "Exit codes: 0 success; 1 invalid input or roundtrip mismatch; 2 I/O\n"
                     "or unknown option.\n",
                     prog, prog, prog);
    return rc < 0 ? 2 : 0;
}

int main(int argc, char *argv[]) {
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "bencode";

#ifdef SIGPIPE
    /* Make the documented "broken pipe -> exit 2" contract hold under
     * default shell SIGPIPE disposition. */
    signal(SIGPIPE, SIG_IGN);
#endif

    if (argc < 2) {
        print_usage(stderr, prog);
        return 2;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        return print_usage(stdout, prog);
    }
    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        if (printf("bencode %s\n", bencode_version()) < 0) {
            return 2;
        }
        return 0;
    }

    /* Recognize -- between subcommand and arguments, though our subcommands
     * never have option-shaped operands so this is mostly defensive. */
    int next_arg = 2;
    if (next_arg < argc && strcmp(argv[next_arg], "--") == 0) {
        next_arg++;
    }
    const char *file = (next_arg < argc) ? argv[next_arg] : NULL;
    if (next_arg + 1 < argc) {
        fprintf(stderr, "%s: extra argument: %s\n", prog, argv[next_arg + 1]);
        print_usage(stderr, prog);
        return 2;
    }

    if (strcmp(cmd, "validate") == 0) {
        return cmd_validate(prog, file);
    }
    if (strcmp(cmd, "print") == 0) {
        return cmd_print(prog, file);
    }
    if (strcmp(cmd, "roundtrip") == 0) {
        return cmd_roundtrip(prog, file);
    }
    if (strcmp(cmd, "size") == 0) {
        return cmd_size(prog, file);
    }

    fprintf(stderr, "%s: unknown subcommand: %s\n", prog, cmd);
    print_usage(stderr, prog);
    return 2;
}
