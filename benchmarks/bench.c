/* clock_gettime / CLOCK_MONOTONIC are POSIX, not C17. Must precede any
 * system header include. */
#if defined(__unix__) || defined(__APPLE__)
#  ifndef _POSIX_C_SOURCE
/* NOLINTNEXTLINE(readability-identifier-naming) */
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 *
 * Microbenchmark for the parse and emit paths.
 *
 * Generates a representative synthetic corpus in memory (so the
 * benchmark binary has no on-disk dependency), then measures:
 *   1. SAX-only parse throughput (no DOM construction).
 *   2. Full DOM parse throughput.
 *   3. Emit throughput from a pre-built DOM tree.
 *   4. Full roundtrip throughput (parse + emit).
 *
 * Each measurement reports MB/s and ops/sec across a fixed wallclock
 * budget. The numbers are wall-clock, single-threaded, on whatever
 * CPU the benchmark binary happens to be running on -- useful as
 * a regression detector, not as an absolute claim.
 */
#include "bencode/bencode.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -- Synthetic corpus ------------------------------------------------------ */

/** Build a representative bencode value tree:
 *   d
 *     "items":  l (N entries; each is a small dict with 5 keys) e
 *     "name":   "synthetic-corpus"
 *     "size":   i<N>e
 *   e
 *
 * Returns the emitted canonical bytes. Caller frees via bencode_buffer_free.
 */
static bencode_status build_corpus(size_t n, uint8_t **out_bytes, size_t *out_len) {
    *out_bytes = NULL;
    *out_len = 0;

    bencode_value *root = NULL;
    bencode_status st = bencode_dict_new(NULL, &root);
    if (st != BENCODE_OK) {
        return st;
    }
    bencode_value *items = NULL;
    st = bencode_list_new(NULL, &items);
    if (st != BENCODE_OK) {
        bencode_value_free(root);
        return st;
    }

    /* Add the small dicts inside `items`. We build each one already
     * sorted; that's the path bencode_dict_set takes O(log n) for. */
    static const char *const keys[] = {"author", "id", "kind", "size", "tag"};
    for (size_t i = 0; i < n; ++i) {
        bencode_value *entry = NULL;
        st = bencode_dict_new(NULL, &entry);
        if (st != BENCODE_OK) {
            break;
        }
        for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
            bencode_value *leaf = NULL;
            if (k == 1U || k == 3U) {
                /* Numeric fields. */
                st = bencode_int_new((bencode_int_t)((i * 7U) + (k * 13U)), NULL, &leaf);
            } else {
                char buf[32];
                int len = snprintf(buf, sizeof buf, "v-%zu-%zu", i, k);
                if (len < 0) {
                    st = BENCODE_ERR_IO;
                    break;
                }
                st = bencode_string_new((const uint8_t *)buf, (size_t)len, NULL, &leaf);
            }
            if (st != BENCODE_OK) {
                break;
            }
            st = bencode_dict_set(entry, (const uint8_t *)keys[k], strlen(keys[k]), leaf);
            if (st != BENCODE_OK) {
                bencode_value_free(leaf);
                break;
            }
        }
        if (st != BENCODE_OK) {
            bencode_value_free(entry);
            break;
        }
        st = bencode_list_append(items, entry);
        if (st != BENCODE_OK) {
            bencode_value_free(entry);
            break;
        }
    }
    if (st != BENCODE_OK) {
        bencode_value_free(items);
        bencode_value_free(root);
        return st;
    }

    /* Now compose the root dict. Order matters for canonical form. */
    if ((st = bencode_dict_set(root, (const uint8_t *)"items", 5, items)) != BENCODE_OK) {
        bencode_value_free(items);
        bencode_value_free(root);
        return st;
    }
    bencode_value *name_val = NULL;
    if ((st = bencode_string_new((const uint8_t *)"synthetic-corpus", 16, NULL, &name_val))
        != BENCODE_OK) {
        bencode_value_free(root);
        return st;
    }
    if ((st = bencode_dict_set(root, (const uint8_t *)"name", 4, name_val)) != BENCODE_OK) {
        bencode_value_free(name_val);
        bencode_value_free(root);
        return st;
    }
    bencode_value *size_val = NULL;
    if ((st = bencode_int_new((bencode_int_t)n, NULL, &size_val)) != BENCODE_OK) {
        bencode_value_free(root);
        return st;
    }
    if ((st = bencode_dict_set(root, (const uint8_t *)"size", 4, size_val)) != BENCODE_OK) {
        bencode_value_free(size_val);
        bencode_value_free(root);
        return st;
    }

    st = bencode_emit_to_alloc(root, NULL, out_bytes, out_len);
    bencode_value_free(root);
    return st;
}

/* -- Timing helpers -------------------------------------------------------- */

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

typedef struct result {
    const char *name;
    size_t iters;
    double seconds;
    size_t bytes_per_iter;
} result;

static void report(const result *r) {
    double mb = ((double)r->iters * (double)r->bytes_per_iter) / (1024.0 * 1024.0);
    double mb_per_s = mb / r->seconds;
    double iters_per_s = (double)r->iters / r->seconds;
    printf("  %-30s  %10zu iter  %10.3f s  %8.2f MB/s  %12.0f iter/s\n",
           r->name, r->iters, r->seconds, mb_per_s, iters_per_s);
}

/* -- Workloads ------------------------------------------------------------- */

static bencode_status sax_drop_all(bencode_int_t v, void *user) {
    (void)v;
    (void)user;
    return BENCODE_OK;
}
static bencode_status sax_drop_str(const uint8_t *b, size_t n, void *user) {
    (void)b;
    (void)n;
    (void)user;
    return BENCODE_OK;
}
static bencode_status sax_drop_marker(void *user) {
    (void)user;
    return BENCODE_OK;
}

static double bench_parse_sax(const uint8_t *bytes, size_t len, double budget_s) {
    bencode_callbacks cb = {sax_drop_all,    sax_drop_str,    sax_drop_marker,
                            sax_drop_marker, sax_drop_marker, sax_drop_marker};
    double t0 = now_seconds();
    size_t iters = 0;
    while (now_seconds() - t0 < budget_s) {
        size_t consumed = 0;
        bencode_status st = bencode_parse_sax(bytes, len, &cb, NULL, 0, &consumed);
        if (st != BENCODE_OK) {
            fprintf(stderr, "bench: SAX parse failed: %s\n", bencode_status_string(st));
            return -1.0;
        }
        ++iters;
    }
    double elapsed = now_seconds() - t0;
    result r = {"parse_sax", iters, elapsed, len};
    report(&r);
    return elapsed;
}

static double bench_parse_dom(const uint8_t *bytes, size_t len, double budget_s) {
    double t0 = now_seconds();
    size_t iters = 0;
    while (now_seconds() - t0 < budget_s) {
        bencode_value *v = NULL;
        bencode_status st = bencode_parse(bytes, len, NULL, &v, NULL);
        if (st != BENCODE_OK) {
            fprintf(stderr, "bench: DOM parse failed: %s\n", bencode_status_string(st));
            bencode_value_free(v);
            return -1.0;
        }
        bencode_value_free(v);
        ++iters;
    }
    double elapsed = now_seconds() - t0;
    result r = {"parse_dom", iters, elapsed, len};
    report(&r);
    return elapsed;
}

static double bench_emit(const uint8_t *bytes, size_t len, double budget_s) {
    /* Pre-parse once so the timed loop only measures emit. */
    bencode_value *v = NULL;
    bencode_status st = bencode_parse(bytes, len, NULL, &v, NULL);
    if (st != BENCODE_OK) {
        fprintf(stderr, "bench: pre-parse for emit failed: %s\n", bencode_status_string(st));
        return -1.0;
    }
    double t0 = now_seconds();
    size_t iters = 0;
    while (now_seconds() - t0 < budget_s) {
        uint8_t *out = NULL;
        size_t out_len = 0;
        bencode_status e = bencode_emit_to_alloc(v, NULL, &out, &out_len);
        if (e != BENCODE_OK) {
            fprintf(stderr, "bench: emit failed: %s\n", bencode_status_string(e));
            bencode_value_free(v);
            return -1.0;
        }
        bencode_buffer_free(NULL, out);
        ++iters;
    }
    double elapsed = now_seconds() - t0;
    bencode_value_free(v);
    result r = {"emit", iters, elapsed, len};
    report(&r);
    return elapsed;
}

static double bench_roundtrip(const uint8_t *bytes, size_t len, double budget_s) {
    double t0 = now_seconds();
    size_t iters = 0;
    while (now_seconds() - t0 < budget_s) {
        bencode_value *v = NULL;
        if (bencode_parse(bytes, len, NULL, &v, NULL) != BENCODE_OK) {
            return -1.0;
        }
        uint8_t *out = NULL;
        size_t out_len = 0;
        if (bencode_emit_to_alloc(v, NULL, &out, &out_len) != BENCODE_OK) {
            bencode_value_free(v);
            return -1.0;
        }
        bencode_buffer_free(NULL, out);
        bencode_value_free(v);
        ++iters;
    }
    double elapsed = now_seconds() - t0;
    result r = {"roundtrip", iters, elapsed, len};
    report(&r);
    return elapsed;
}

/* -- Main ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    size_t n = 5000;          /* number of items in the synthetic corpus */
    double budget_s = 1.0;    /* wallclock budget per measurement */

    if (argc > 1) {
        n = (size_t)strtoull(argv[1], NULL, 10);
    }
    if (argc > 2) {
        budget_s = strtod(argv[2], NULL);
    }

    uint8_t *corpus = NULL;
    size_t corpus_len = 0;
    bencode_status st = build_corpus(n, &corpus, &corpus_len);
    if (st != BENCODE_OK) {
        fprintf(stderr, "bench: corpus build failed: %s\n", bencode_status_string(st));
        return 1;
    }
    printf("corpus: %zu items, %zu bytes (%.2f KB); budget: %.1f s per measurement\n", n,
           corpus_len, (double)corpus_len / 1024.0, budget_s);
    printf("  %-30s  %10s  %10s  %8s  %12s\n", "workload", "iters", "elapsed", "MB/s", "iter/s");

    bench_parse_sax(corpus, corpus_len, budget_s);
    bench_parse_dom(corpus, corpus_len, budget_s);
    bench_emit(corpus, corpus_len, budget_s);
    bench_roundtrip(corpus, corpus_len, budget_s);

    bencode_buffer_free(NULL, corpus);
    return 0;
}
