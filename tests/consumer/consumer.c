/*
 * SPDX-License-Identifier: MIT
 *
 * Downstream consumer used by CI to verify find_package(bencode)
 * resolves cleanly against an installed package.
 */
#include <bencode/bencode.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *literal = "d3:cow3:mooe";
    bencode_value *v = NULL;
    size_t consumed = 0;
    bencode_status st =
        bencode_parse((const uint8_t *)literal, strlen(literal), NULL, &v, &consumed);
    if (st != BENCODE_OK) {
        fprintf(stderr, "consumer: parse failed: %s\n", bencode_status_string(st));
        return 1;
    }
    printf("linked against libbencode %s; consumed=%zu\n", bencode_version(), consumed);
    bencode_value_free(v);
    return 0;
}
