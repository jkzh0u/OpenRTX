/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_CHLIST_H
#define OPENRTX_CHLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Channel list image: the channels of memory mode, and the TETRA talkgroup
 * names, filters and location area names, in one binary image kept apart
 * from the firmware (on the C62, in its own flash area). One firmware then
 * serves everyone, each with their own list.
 *
 * scripts/chlist.py builds an image from a list header, the format of
 * platform/drivers/CPS/cps_io_builtin.c and
 * openrtx/include/protocols/TETRA/TetraIds.h. The image is read in place:
 * names point into it.
 *
 * Layout, little endian, every record 4-byte aligned:
 *
 *   header, 32 bytes:
 *     0  magic "OCHL"
 *     4  u16 version (1)
 *     6  u16 header size (32)
 *     8  u32 image size, header included
 *    12  u32 CRC-32 (IEEE) of the bytes after the header
 *    16  u16 channels, u16 filters, u16 identities, u16 location areas,
 *        u16 identity ranges, u16 reserved (0)
 *    28  u32 offset of the string table from the start of the image
 *   then the records, in that order:
 *     channel 20 bytes: u32 rx Hz, u32 tx Hz, u32 name, u8 mode, u8
 *       bandwidth (enum bandwidth), u8 flags (bit 0: receive only), u8 rx
 *       tone, u8 tx tone (bit 7: enabled, bits 6-0: CTCSS index), 3 x u8 0
 *     filter 4 bytes: u32 name (filter 1 onwards; 0 is "All")
 *     identity 12 bytes: u32 SSI, u32 name, u32 filter mask
 *     location area 8 bytes: u16 LA, u16 0, u32 name
 *     identity range 20 bytes: u32 first, u32 last, u32 name format, u32
 *       base, u32 filter mask
 *   then the strings, each ending with a NUL; names are offsets into them.
 *
 * Filter masks: bit n for filter n, bit 0 for an alarm talkgroup. A range's
 * name format has exactly one %u (with optional flags and width), given
 * the SSI minus the base.
 */

#define CHLIST_VERSION 1
#define CHLIST_HEADER_SIZE 32
/** Filters an image can define, besides "All" */
#define CHLIST_MAX_FILTERS 30

#define CHLIST_RX_ONLY 0x01

struct chlistChannel {
    uint32_t rxHz;
    uint32_t txHz;
    const char *name;
    uint8_t mode;
    uint8_t bandwidth;
    uint8_t flags;
    uint8_t rxTone;
    uint8_t txTone;
};

struct chlistId {
    uint32_t ssi;
    const char *name;
    uint32_t filters;
};

struct chlistLa {
    uint16_t la;
    const char *name;
};

struct chlistRange {
    uint32_t first;
    uint32_t last;
    const char *format;
    uint32_t base;
    uint32_t filters;
};

/**
 * Use an image. Checks it whole first: without a valid image the list is
 * empty.
 *
 * @param image: start of the image, 4-byte aligned; it must stay readable.
 * @param maxSize: bytes readable from image.
 * @return 0 if the image is used, a negative number saying why not: -1 no
 * image (erased flash), -2 other version, -3 bad size or layout, -4 bad
 * CRC, -5 bad string or name format.
 */
int chlist_open(const void *image, size_t maxSize);

/**
 * Stop using the image: the list is empty.
 */
void chlist_close(void);

/** @return true if an image is in use. */
bool chlist_valid(void);

size_t chlist_channelCount(void);
size_t chlist_filterCount(void); /**< Filters besides "All" */
size_t chlist_idCount(void);
size_t chlist_laCount(void);
size_t chlist_rangeCount(void);

/**
 * Records by index, 0 onwards. Each returns false past the end.
 */
bool chlist_channel(size_t i, struct chlistChannel *out);
const char *chlist_filterName(size_t i); /**< i = 0 is filter 1 */
bool chlist_id(size_t i, struct chlistId *out);
bool chlist_la(size_t i, struct chlistLa *out);
bool chlist_range(size_t i, struct chlistRange *out);

/**
 * CRC-32 (IEEE 802.3, as zlib's crc32()).
 */
uint32_t chlist_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_CHLIST_H */
