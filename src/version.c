/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 bencode contributors
 */
/**
 * @file version.c
 * @brief Implementation of ::bencode_version.
 */
#include "bencode/bencode.h"

const char *bencode_version(void) {
    return BENCODE_VERSION_STRING;
}
