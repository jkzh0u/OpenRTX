/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * C62 hardware self test (interfaces/selftest.h): every check only reads,
 * except the PSRAM one, on a buffer of its own.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include "interfaces/selftest.h"
#include "core/chlist.h"
#include "core/memch.h"
#include "core/state.h"
#include "drivers/audio/csk6_codec.h"
#include "protocols/TETRA/TetraCallStore.h"
#include "hwconfig.h"

/* platform.c */
extern int c62_dspResult;

/* BK4819 registers shown and checked */
#define BK4819_REGS 0x80

/* A two cell Li-ion pack between flat and full, mV */
#define VBAT_MIN 6000
#define VBAT_MAX 8800

/* Written and read back to check the PSRAM */
static uint32_t psramTest[256] __attribute__((section(".psram_section")));

static struct selftestItem *add(struct selftestItem *items, size_t *n,
                                size_t max, const char *name)
{
    if (*n >= max)
        return NULL;
    struct selftestItem *it = &items[(*n)++];
    memset(it, 0, sizeof(*it));
    snprintf(it->name, sizeof(it->name), "%s", name);
    it->result = SELFTEST_INFO;
    return it;
}

#define ITEM(name)                                       \
    struct selftestItem *it = add(items, &n, max, name); \
    if (it == NULL)                                      \
    return n

/** The BK4819 answers: its registers are not all the same */
static bool bk4819Answers(void)
{
    uint16_t first = BK4819_readReg(&c62_bk4819, 0);
    for (uint8_t r = 1; r < BK4819_REGS; r++)
        if (BK4819_readReg(&c62_bk4819, r) != first)
            return true;
    return false;
}

/** @return the index of the first word of the PSRAM test that is wrong,
 * or -1 */
static int psramCheck(void)
{
    static const uint32_t patterns[] = { 0x55555555, 0xaaaaaaaa, 0 };
    const size_t words = sizeof(psramTest) / sizeof(psramTest[0]);
    volatile uint32_t *p = psramTest;

    for (unsigned k = 0; k < sizeof(patterns) / sizeof(patterns[0]); k++) {
        for (size_t i = 0; i < words; i++)
            p[i] = patterns[k] ? patterns[k] : (uint32_t)i * 0x9e3779b9u;
        for (size_t i = 0; i < words; i++) {
            uint32_t want = patterns[k] ? patterns[k] :
                                          (uint32_t)i * 0x9e3779b9u;
            if (p[i] != want)
                return (int)i;
        }
    }
    return -1;
}

size_t selftest_run(struct selftestItem *items, size_t max)
{
    size_t n = 0;

    {
        ITEM("Radio chip");
        bool ok = bk4819Answers();
        snprintf(it->value, sizeof(it->value), "%s",
                 ok ? "answers" : "no answer");
        it->result = ok ? SELFTEST_PASS : SELFTEST_FAIL;
    }

#ifdef CONFIG_BCAST_FM
    {
        /* Register 1 is the chip ID */
        ITEM("FM chip");
        uint16_t id = BK1080_ReadRegister(&c62_bk1080, (BK1080_Register_t)1);
        snprintf(it->value, sizeof(it->value), "ID %04x", id);
        it->result = (id == 0x1080) ? SELFTEST_PASS : SELFTEST_FAIL;
    }
#endif

    {
        ITEM("PSRAM");
        int bad = psramCheck();
        if (bad < 0)
            snprintf(it->value, sizeof(it->value), "%u KB ok",
                     (unsigned)sizeof(psramTest));
        else
            snprintf(it->value, sizeof(it->value), "bad at %d", bad * 4);
        it->result = (bad < 0) ? SELFTEST_PASS : SELFTEST_FAIL;
    }

    {
        ITEM("DSP");
        if (c62_dspResult == 0)
            snprintf(it->value, sizeof(it->value), "started");
        else
            snprintf(it->value, sizeof(it->value), "error %d", c62_dspResult);
        it->result = (c62_dspResult == 0) ? SELFTEST_PASS : SELFTEST_FAIL;
    }

    {
        ITEM("Audio");
        uint32_t before = csk6codec_loops();
        k_msleep(20);
        uint32_t loops = csk6codec_loops() - before;
        int ret = csk6codec_initResult();
        if (ret != 0) {
            snprintf(it->value, sizeof(it->value), "error %d", ret);
            it->result = SELFTEST_FAIL;
        } else if (loops == 0) {
            snprintf(it->value, sizeof(it->value), "stalled");
            it->result = SELFTEST_FAIL;
        } else {
            snprintf(it->value, sizeof(it->value), "running");
            it->result = SELFTEST_PASS;
        }
    }

    {
        ITEM("Battery");
        uint16_t mv = state.v_bat;
        snprintf(it->value, sizeof(it->value), "%u.%02u V", mv / 1000,
                 mv % 1000 / 10);
        it->result = (mv >= VBAT_MIN && mv <= VBAT_MAX) ? SELFTEST_PASS :
                                                          SELFTEST_FAIL;
    }

    {
        ITEM("Ch list");
        if (chlist_valid())
            snprintf(it->value, sizeof(it->value), "%u ch, %u names",
                     (unsigned)chlist_channelCount(),
                     (unsigned)chlist_idCount());
        else
            snprintf(it->value, sizeof(it->value), "none");
    }

#ifdef CONFIG_MEMCH
    {
        ITEM("Memories");
        if (memch_ready())
            snprintf(it->value, sizeof(it->value), "%u saved", memch_count());
        else
            snprintf(it->value, sizeof(it->value), "not read");
        it->result = memch_ready() ? SELFTEST_PASS : SELFTEST_FAIL;
    }
#endif

#ifdef CONFIG_TETRA_CALLREC
    {
        ITEM("Saved calls");
        if (callStore_ready())
            snprintf(it->value, sizeof(it->value), "%u saved",
                     callStore_count());
        else
            snprintf(it->value, sizeof(it->value), "not read");
        it->result = callStore_ready() ? SELFTEST_PASS : SELFTEST_FAIL;
    }
#endif

    return n;
}

bool selftest_reg(uint16_t n, uint8_t *addr, uint16_t *value)
{
    if (n >= BK4819_REGS)
        return false;
    *addr = (uint8_t)n;
    if (value != NULL)
        *value = BK4819_readReg(&c62_bk4819, (uint8_t)n);
    return true;
}
