/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal example: parse a small bencode literal and walk the result.
 */
#include "bencode/bencode.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    /* d3:cow3:moo4:spaml1:a1:bee = { "cow": "moo", "spam": ["a", "b"] } */
    const char *literal = "d3:cow3:moo4:spaml1:a1:bee";
    const uint8_t *input = (const uint8_t *)literal;

    bencode_value *root = NULL;
    size_t consumed = 0;
    bencode_status st = bencode_parse(input, strlen(literal), NULL, &root, &consumed);
    if (st != BENCODE_OK) {
        fprintf(stderr, "parse failed at offset %zu: %s\n", consumed, bencode_status_string(st));
        return 1;
    }

    printf("parsed %zu bytes into bencode tree\n", consumed);
    printf("top-level type: %d (4 = dict)\n", (int)bencode_value_type(root));

    /* Look up "cow". */
    const uint8_t *cow_key = (const uint8_t *)"cow";
    const bencode_value *cow_val = bencode_dict_get(root, cow_key, 3);
    if (cow_val != NULL) {
        const uint8_t *bytes = NULL;
        size_t len = 0;
        if (bencode_value_string(cow_val, &bytes, &len) == BENCODE_OK) {
            printf("cow -> \"%.*s\"\n", (int)len, (const char *)bytes);
        }
    }

    bencode_value_free(root);
    return 0;
}
