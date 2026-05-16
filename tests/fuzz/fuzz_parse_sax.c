/*
 * SPDX-License-Identifier: MIT
 *
 * libFuzzer harness for the SAX parser. The lowest-level entry point; no
 * allocation in the harness so libFuzzer's leak detection is meaningful.
 *
 * The bencode library makes a strong claim: for any byte sequence,
 * bencode_parse_sax either returns BENCODE_OK or a specific error code,
 * and never reads outside the input range. This harness exercises every
 * input the mutator can produce; ASan + UBSan in the same build catch
 * any violation of that claim.
 */
#include "bencode/bencode.h"

#include <stddef.h>
#include <stdint.h>

/* NOLINTNEXTLINE(readability-identifier-naming) */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* NOLINTNEXTLINE(readability-identifier-naming) */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t consumed = 0;
    (void)bencode_parse_sax(data, size, NULL, NULL, 0, &consumed);
    return 0;
}
