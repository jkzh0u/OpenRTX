/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "core/chlist.h"

#define CHANNEL_SIZE 20
#define FILTER_SIZE 4
#define ID_SIZE 12
#define LA_SIZE 8
#define RANGE_SIZE 20

/* The image in use, NULL if none */
static const uint8_t *img;
static uint32_t strOffset;
static uint16_t nChannels, nFilters, nIds, nLas, nRanges;
static uint32_t channelsAt, filtersAt, idsAt, lasAt, rangesAt;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

uint32_t chlist_crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xffffffffu;

    while (len--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }

    return ~crc;
}

/** A string reference of a candidate image: inside the string table */
static bool strOk(uint32_t strAt, uint32_t size, uint32_t ref)
{
    return ref < size - strAt;
}

/**
 * A range's name format: exactly one %u, with optional flags 0 or -, and a
 * width of up to two digits; %% allowed.
 */
static bool formatOk(const char *f)
{
    unsigned conversions = 0;

    while (*f != '\0') {
        if (*f++ != '%')
            continue;
        if (*f == '%') {
            f++;
            continue;
        }
        while (*f == '0' || *f == '-')
            f++;
        for (int d = 0; d < 2 && *f >= '0' && *f <= '9'; d++)
            f++;
        if (*f != 'u')
            return false;
        f++;
        conversions++;
    }

    return conversions == 1;
}

int chlist_open(const void *image, size_t maxSize)
{
    const uint8_t *p = image;

    chlist_close();
    if (p == NULL || maxSize < CHLIST_HEADER_SIZE)
        return -1;
    if (memcmp(p, "OCHL", 4) != 0)
        return -1;
    if (rd16(p + 4) < CHLIST_VERSION_MIN || rd16(p + 4) > CHLIST_VERSION
        || rd16(p + 6) != CHLIST_HEADER_SIZE)
        return -2;

    uint32_t size = rd32(p + 8);
    uint16_t nc = rd16(p + 16), nf = rd16(p + 18), ni = rd16(p + 20);
    uint16_t nl = rd16(p + 22), nr = rd16(p + 24);
    uint32_t strAt = rd32(p + 28);

    /* Records fill the space up to the strings, which end the image */
    uint32_t end = CHLIST_HEADER_SIZE + (uint32_t)nc * CHANNEL_SIZE
                 + (uint32_t)nf * FILTER_SIZE + (uint32_t)ni * ID_SIZE
                 + (uint32_t)nl * LA_SIZE + (uint32_t)nr * RANGE_SIZE;
    if (size > maxSize || nf > CHLIST_MAX_FILTERS || strAt != end
        || strAt >= size || p[size - 1] != '\0')
        return -3;

    if (chlist_crc32(p + CHLIST_HEADER_SIZE, size - CHLIST_HEADER_SIZE)
        != rd32(p + 12))
        return -4;

    /* Every name inside the string table, every range format safe */
    uint32_t at = CHLIST_HEADER_SIZE;
    for (unsigned i = 0; i < nc; i++, at += CHANNEL_SIZE) {
        if (!strOk(strAt, size, rd32(p + at + 8)))
            return -5;
    }
    for (unsigned i = 0; i < nf; i++, at += FILTER_SIZE) {
        if (!strOk(strAt, size, rd32(p + at)))
            return -5;
    }
    for (unsigned i = 0; i < ni; i++, at += ID_SIZE) {
        if (!strOk(strAt, size, rd32(p + at + 4)))
            return -5;
    }
    for (unsigned i = 0; i < nl; i++, at += LA_SIZE) {
        if (!strOk(strAt, size, rd32(p + at + 4)))
            return -5;
    }
    for (unsigned i = 0; i < nr; i++, at += RANGE_SIZE) {
        uint32_t ref = rd32(p + at + 8);
        if (!strOk(strAt, size, ref)
            || !formatOk((const char *)p + strAt + ref))
            return -5;
    }

    nChannels = nc;
    nFilters = nf;
    nIds = ni;
    nLas = nl;
    nRanges = nr;
    channelsAt = CHLIST_HEADER_SIZE;
    filtersAt = channelsAt + (uint32_t)nc * CHANNEL_SIZE;
    idsAt = filtersAt + (uint32_t)nf * FILTER_SIZE;
    lasAt = idsAt + (uint32_t)ni * ID_SIZE;
    rangesAt = lasAt + (uint32_t)nl * LA_SIZE;
    strOffset = strAt;
    img = p;

    return 0;
}

void chlist_close(void)
{
    img = NULL;
    nChannels = nFilters = nIds = nLas = nRanges = 0;
}

bool chlist_valid(void)
{
    return img != NULL;
}

static const char *str(uint32_t ref)
{
    return (const char *)img + strOffset + ref;
}

size_t chlist_channelCount(void)
{
    return nChannels;
}

size_t chlist_filterCount(void)
{
    return nFilters;
}

size_t chlist_idCount(void)
{
    return nIds;
}

size_t chlist_laCount(void)
{
    return nLas;
}

size_t chlist_rangeCount(void)
{
    return nRanges;
}

bool chlist_channel(size_t i, struct chlistChannel *out)
{
    if (i >= nChannels)
        return false;

    const uint8_t *r = img + channelsAt + i * CHANNEL_SIZE;
    out->rxHz = rd32(r);
    out->txHz = rd32(r + 4);
    out->name = str(rd32(r + 8));
    out->mode = r[12];
    out->bandwidth = r[13];
    out->flags = r[14];
    out->rxTone = r[15];
    out->txTone = r[16];
    return true;
}

const char *chlist_filterName(size_t i)
{
    if (i >= nFilters)
        return NULL;

    return str(rd32(img + filtersAt + i * FILTER_SIZE));
}

bool chlist_id(size_t i, struct chlistId *out)
{
    if (i >= nIds)
        return false;

    const uint8_t *r = img + idsAt + i * ID_SIZE;
    uint32_t ssi = rd32(r);
    out->ssi = ssi & 0xffffff;
    out->priority = (uint8_t)(ssi >> 24);
    out->name = str(rd32(r + 4));
    out->filters = rd32(r + 8);
    return true;
}

bool chlist_la(size_t i, struct chlistLa *out)
{
    if (i >= nLas)
        return false;

    const uint8_t *r = img + lasAt + i * LA_SIZE;
    out->la = rd16(r);
    out->name = str(rd32(r + 4));
    return true;
}

bool chlist_range(size_t i, struct chlistRange *out)
{
    if (i >= nRanges)
        return false;

    const uint8_t *r = img + rangesAt + i * RANGE_SIZE;
    uint32_t first = rd32(r);
    out->first = first & 0xffffff;
    out->priority = (uint8_t)(first >> 24);
    out->last = rd32(r + 4) & 0xffffff;
    out->format = str(rd32(r + 8));
    out->base = rd32(r + 12);
    out->filters = rd32(r + 16);
    return true;
}
