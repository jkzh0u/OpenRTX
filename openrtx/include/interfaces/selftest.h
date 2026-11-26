/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INTERFACES_SELFTEST_H
#define INTERFACES_SELFTEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hardware self test (Menu > Self test, CONFIG_SELFTEST): checks that each
 * part of the radio answers, and the radio chip's registers, to find what
 * is wrong on a new radio. The checks only read, they change nothing.
 */

enum selftestResult {
    SELFTEST_INFO = 0, /**< A figure, neither good nor bad */
    SELFTEST_PASS,     /**< The part works */
    SELFTEST_FAIL,     /**< The part does not answer, or answers wrong */
};

struct selftestItem {
    char name[12];  /**< The part: "BK4819", "PSRAM" */
    char value[24]; /**< What was found: "answers", "0x1080" */
    uint8_t result; /**< enum selftestResult */
};

/**
 * Run the checks of this radio's hardware. Takes a few milliseconds: called
 * when the page opens and on Green.
 *
 * @param items: where to put the results.
 * @param max: room in items.
 * @return the number of results.
 */
size_t selftest_run(struct selftestItem *items, size_t max);

/**
 * A register of the radio chip, for the register page.
 *
 * @param n: 0 for the first register shown.
 * @param addr: its address.
 * @param value: its value now; NULL to only learn whether there is one.
 * @return false past the last, or if the radio has none to show.
 */
bool selftest_reg(uint16_t n, uint8_t *addr, uint16_t *value);

#ifdef __cplusplus
}
#endif

#endif /* INTERFACES_SELFTEST_H */
