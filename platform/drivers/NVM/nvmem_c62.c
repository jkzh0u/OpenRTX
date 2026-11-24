/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Retevis C62 nonvolatile memory: settings and VFO in an emulated EEPROM
 * (eeep) on the CSK6 SPI flash, as on the CS7000.
 *
 * The flash also holds the firmware (executed in place), the DSP image and
 * the OEM factory resources, calibration included. Only the first 16 KiB of
 * the devicetree's storage_partition (0x2B0000, erased in every factory
 * dump examined) is used: eeep erases its whole partition when it finds no
 * valid page, so the partition below must stay inside storage_partition.
 */

#include <string.h>
#include <zephyr/storage/flash_map.h>
#include "interfaces/nvmem.h"
#include "core/nvmem_access.h"
#include "core/nvmem_device.h"
#include "core/crc.h"
#include "drivers/NVM/eeep.h"
#include "drivers/NVM/flash_zephyr.h"
#include "hwconfig.h"
#if defined(CONFIG_MEMCH) || defined(CONFIG_TETRA_CALLREC)
#include "core/chlist.h"
#endif
#ifdef CONFIG_TETRA_CALLREC
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "protocols/TETRA/TetraCallStore.h"
#endif
#ifdef CONFIG_MEMCH
#include <stdio.h>
#include "core/memch.h"
#endif

#define C62_EEEP_OFFSET FIXED_PARTITION_OFFSET(storage_partition)
#define C62_EEEP_SIZE (16 * 1024)

BUILD_ASSERT(C62_EEEP_SIZE <= FIXED_PARTITION_SIZE(storage_partition),
             "eeep area larger than the storage partition");

/* eeep record ids, as on the CS7000 */
#define EEEP_VFO 0x0001
#define EEEP_SETTINGS 0x0002

/*
 * The flash controller that holds storage_partition (the "flash" alias
 * names the memory node, which is not a device)
 */
static struct nvmInfo eflashInfo;
static const struct nvmDevice eflash = {
    .priv = FIXED_PARTITION_DEVICE(storage_partition),
    .ops = &zephyr_flash_ops,
    .info = &eflashInfo,
};
EEEP_DEVICE_DEFINE(eeep)

static const struct nvmPartition memPartitions[] = {
    {
        .offset = C62_EEEP_OFFSET, // eeep storage
        .size = C62_EEEP_SIZE,
    },
};

static const struct nvmDescriptor memAreas[] = {
    {
        .name = "SPI flash",
        .dev = &eflash,
        .baseAddr = 0x00000000,
        .size = 0x400000, // 4 MiB
        .nbPart = ARRAY_SIZE(memPartitions),
        .partitions = memPartitions,
    },
    {
        .name = "Virtual EEPROM",
        .dev = &eeep,
        .baseAddr = 0x00000000,
        .size = 0xFFFFFFFF, // No physical size
        .nbPart = 0,
        .partitions = NULL,
    },
};

const struct nvmTable nvmTab = {
    .areas = memAreas,
    .nbAreas = ARRAY_SIZE(memAreas),
};

#ifdef CONFIG_MEMCH
/*
 * Saved FM channels (core/memch.h), after the eeep area in
 * storage_partition: two banks of 8 KiB, written in turn. A bank is the
 * slot table then, written last, a header with a sequence number and the
 * table's CRC: at power on the valid bank with the highest sequence number
 * is used, so a write cut short leaves the previous table.
 */
#define MEMCH_BANK_SIZE (8 * 1024)
#define MEMCH_BANK0 (C62_EEEP_OFFSET + C62_EEEP_SIZE)
#define MEMCH_MAGIC 0x48434d4fu /* "OMCH" */
#define MEMCH_VERSION 1

BUILD_ASSERT(C62_EEEP_SIZE + 2 * MEMCH_BANK_SIZE
                 <= FIXED_PARTITION_SIZE(storage_partition),
             "saved channels outside the storage partition");

/* One slot, 32 bytes */
struct memchRecord {
    char name[MEMCH_NAME_LEN + 1];
    uint32_t rx;
    uint32_t tx;
    uint8_t flags; /* MEMCH_F_... */
    uint8_t rxTone;
    uint8_t txTone;
    uint8_t power; /* Hundreds of mW */
    uint8_t reserved[4];
} __attribute__((packed));

#define MEMCH_F_USED 0x01
#define MEMCH_F_WIDE 0x02
#define MEMCH_F_RX_TONE 0x04
#define MEMCH_F_TX_TONE 0x08
#define MEMCH_F_RX_DCS 0x10
#define MEMCH_F_TX_DCS 0x20
#define MEMCH_F_RX_INV 0x40
#define MEMCH_F_TX_INV 0x80

struct memchHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t slots;
    uint32_t seq;
    uint32_t crc;
} __attribute__((packed));

BUILD_ASSERT(sizeof(struct memchHeader)
                     + MEMCH_SLOTS * sizeof(struct memchRecord)
                 <= MEMCH_BANK_SIZE,
             "saved channel table larger than a bank");

static struct memchRecord memTable[MEMCH_SLOTS];
static uint32_t memSeq;
static uint8_t memBank; /* Bank holding the current table */
static bool memReady;

static void memchLoad(void)
{
    int best = -1;
    struct memchHeader h, bestH = { 0 };

    memset(memTable, 0, sizeof(memTable));
    memReady = true;
    for (int b = 0; b < 2; b++) {
        uint32_t base = MEMCH_BANK0 + (uint32_t)b * MEMCH_BANK_SIZE;
        if (nvm_devRead(&eflash, base, &h, sizeof(h)) < 0)
            continue;
        if (h.magic != MEMCH_MAGIC || h.version != MEMCH_VERSION
            || h.slots != MEMCH_SLOTS)
            continue;
        if (best >= 0 && (int32_t)(h.seq - bestH.seq) <= 0)
            continue;
        if (nvm_devRead(&eflash, base + sizeof(h), memTable, sizeof(memTable))
            < 0)
            continue;
        if (chlist_crc32(memTable, sizeof(memTable)) != h.crc)
            continue;
        best = b;
        bestH = h;
    }

    if (best < 0) {
        memset(memTable, 0, sizeof(memTable));
        memSeq = 0;
        memBank = 1; /* The first write goes to bank 0 */
        return;
    }

    /* Read the chosen bank again: the loop may have left the other one */
    nvm_devRead(&eflash,
                MEMCH_BANK0 + (uint32_t)best * MEMCH_BANK_SIZE
                    + sizeof(struct memchHeader),
                memTable, sizeof(memTable));
    memSeq = bestH.seq;
    memBank = (uint8_t)best;
}

/** Write the table to the other bank, then make it the current one */
static int memchStore(void)
{
    uint8_t bank = memBank ^ 1;
    uint32_t base = MEMCH_BANK0 + (uint32_t)bank * MEMCH_BANK_SIZE;
    struct memchHeader h = {
        .magic = MEMCH_MAGIC,
        .version = MEMCH_VERSION,
        .slots = MEMCH_SLOTS,
        .seq = memSeq + 1,
        .crc = chlist_crc32(memTable, sizeof(memTable)),
    };

    if (nvm_devErase(&eflash, base, MEMCH_BANK_SIZE) < 0)
        return -1;
    if (nvm_devWrite(&eflash, base + sizeof(h), memTable, sizeof(memTable)) < 0)
        return -1;
    if (nvm_devWrite(&eflash, base, &h, sizeof(h)) < 0)
        return -1;

    memSeq = h.seq;
    memBank = bank;
    return 0;
}

bool memch_ready(void)
{
    return memReady;
}

bool memch_used(uint16_t slot)
{
    return slot < MEMCH_SLOTS && (memTable[slot].flags & MEMCH_F_USED);
}

bool memch_read(uint16_t slot, channel_t *ch)
{
    if (!memch_used(slot))
        return false;

    const struct memchRecord *r = &memTable[slot];
    memset(ch, 0, sizeof(*ch));
    ch->mode = OPMODE_FM;
    ch->bandwidth = (r->flags & MEMCH_F_WIDE) ? BW_25 : BW_12_5;
    ch->rx_frequency = r->rx;
    ch->tx_frequency = r->tx;
    ch->power = (uint32_t)r->power * 100;
    ch->fm.rxToneEn = (r->flags & MEMCH_F_RX_TONE) ? 1 : 0;
    ch->fm.txToneEn = (r->flags & MEMCH_F_TX_TONE) ? 1 : 0;
    ch->fm.rxTone = r->rxTone & 0x7f;
    ch->fm.txTone = r->txTone & 0x7f;
    ch->rxDcs = (r->flags & MEMCH_F_RX_DCS) ? 1 : 0;
    ch->txDcs = (r->flags & MEMCH_F_TX_DCS) ? 1 : 0;
    ch->rxDcsInv = (r->flags & MEMCH_F_RX_INV) ? 1 : 0;
    ch->txDcsInv = (r->flags & MEMCH_F_TX_INV) ? 1 : 0;
    strncpy(ch->name, r->name, sizeof(ch->name) - 1);
    return true;
}

int memch_save(uint16_t slot, const channel_t *ch)
{
    if (slot >= MEMCH_SLOTS || ch->mode != OPMODE_FM)
        return -1;

    struct memchRecord r;
    memset(&r, 0, sizeof(r));
    r.rx = ch->rx_frequency;
    r.tx = ch->tx_frequency;
    r.flags = MEMCH_F_USED | ((ch->bandwidth == BW_25) ? MEMCH_F_WIDE : 0)
            | (ch->fm.rxToneEn ? MEMCH_F_RX_TONE : 0)
            | (ch->fm.txToneEn ? MEMCH_F_TX_TONE : 0)
            | (ch->rxDcs ? MEMCH_F_RX_DCS : 0)
            | (ch->txDcs ? MEMCH_F_TX_DCS : 0)
            | (ch->rxDcsInv ? MEMCH_F_RX_INV : 0)
            | (ch->txDcsInv ? MEMCH_F_TX_INV : 0);
    r.rxTone = ch->fm.rxTone;
    r.txTone = ch->fm.txTone;
    uint32_t p = (ch->power + 50) / 100;
    r.power = (uint8_t)((p > 255) ? 255 : p);
    if (ch->name[0] != '\0')
        strncpy(r.name, ch->name, MEMCH_NAME_LEN);
    else
        snprintf(r.name, sizeof(r.name), "%lu.%05lu",
                 (unsigned long)(ch->rx_frequency / 1000000),
                 (unsigned long)(ch->rx_frequency % 1000000 / 10));

    struct memchRecord old = memTable[slot];
    memTable[slot] = r;
    if (memchStore() < 0) {
        memTable[slot] = old;
        return -1;
    }
    return 0;
}

int memch_rename(uint16_t slot, const char *name)
{
    if (!memch_used(slot))
        return -1;

    struct memchRecord old = memTable[slot];
    memset(memTable[slot].name, 0, sizeof(memTable[slot].name));
    strncpy(memTable[slot].name, name, MEMCH_NAME_LEN);
    if (memchStore() < 0) {
        memTable[slot] = old;
        return -1;
    }
    return 0;
}

int memch_delete(uint16_t slot)
{
    if (!memch_used(slot))
        return 0;

    struct memchRecord old = memTable[slot];
    memset(&memTable[slot], 0, sizeof(memTable[slot]));
    if (memchStore() < 0) {
        memTable[slot] = old;
        return -1;
    }
    return 0;
}

uint16_t memch_count(void)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < MEMCH_SLOTS; i++)
        n += memch_used(i) ? 1 : 0;
    return n;
}

int memch_nth(uint16_t n)
{
    for (uint16_t i = 0; i < MEMCH_SLOTS; i++) {
        if (memch_used(i) && n-- == 0)
            return i;
    }
    return -1;
}
#endif /* CONFIG_MEMCH */

#ifdef CONFIG_TETRA_CALLREC
/*
 * TETRA calls saved on the radio (TetraCallStore.h), in
 * call_store_partition: each call from the start of a 4 kB sector, a
 * header then its speech frames, as many sectors as it takes. The header is
 * written last, so that a call cut short by a power loss is not seen. A
 * new call goes after the newest one, back at the start when it would not
 * fit before the end, and the calls it writes over are dropped. Deleting
 * clears a word of the header.
 */
#define CALL_BASE FIXED_PARTITION_OFFSET(call_store_partition)
#define CALL_SIZE FIXED_PARTITION_SIZE(call_store_partition)
#define CALL_SECTOR 4096u
#define CALL_SECTORS (CALL_SIZE / CALL_SECTOR)
#define CALL_MAGIC 0x4c414354u /* "TCAL" */
#define CALL_HDR 64u
/* Written a page at a time, with a pause after each erase and page */
#define CALL_PAGE 256u

struct callHeader {
    uint32_t magic;
    uint32_t live; /* 0xffffffff, 0 once deleted */
    struct callStoreInfo info;
    uint32_t crc;  /* Of info */
} __attribute__((packed));

BUILD_ASSERT(sizeof(struct callHeader) <= CALL_HDR, "call header too long");
BUILD_ASSERT(CALL_SECTORS <= 255, "call store too large for the index");

/* The calls saved, newest first */
static struct {
    uint32_t seq;
    uint8_t sector;
    uint8_t sectors;
} calls[CALL_SECTORS];
static uint16_t nCalls;
static uint32_t nextSeq = 1;
static bool callsReady;
static K_MUTEX_DEFINE(callMutex);

static uint8_t sectorsOf(uint32_t frames)
{
    uint32_t bytes = CALL_HDR + frames * CALLSTORE_FRAME_BYTES;
    return (uint8_t)((bytes + CALL_SECTOR - 1) / CALL_SECTOR);
}

static uint32_t sectorAddr(uint8_t sector)
{
    return CALL_BASE + (uint32_t)sector * CALL_SECTOR;
}

/** Insert a call in the index, newest first */
static void indexAdd(uint32_t seq, uint8_t sector, uint8_t sectors)
{
    uint16_t i = nCalls;
    while (i > 0 && calls[i - 1].seq < seq) {
        calls[i] = calls[i - 1];
        i--;
    }
    calls[i].seq = seq;
    calls[i].sector = sector;
    calls[i].sectors = sectors;
    nCalls++;
}

static void indexRemove(uint16_t i)
{
    for (; i + 1 < nCalls; i++)
        calls[i] = calls[i + 1];
    nCalls--;
}

static int indexFind(uint32_t seq)
{
    for (uint16_t i = 0; i < nCalls; i++) {
        if (calls[i].seq == seq)
            return i;
    }
    return -1;
}

static bool readHeader(uint8_t sector, struct callHeader *h)
{
    if (nvm_devRead(&eflash, sectorAddr(sector), h, sizeof(*h)) < 0)
        return false;
    return h->magic == CALL_MAGIC
        && h->crc == chlist_crc32(&h->info, sizeof(h->info))
        && h->info.frames > 0
        && sector + sectorsOf(h->info.frames) <= CALL_SECTORS;
}

/** Find the calls saved: at start-up */
static void callsLoad(void)
{
    struct callHeader h;

    nCalls = 0;
    for (uint8_t s = 0; s < CALL_SECTORS;) {
        if (!readHeader(s, &h)) {
            s++;
            continue;
        }
        uint8_t n = sectorsOf(h.info.frames);
        if (h.live == 0xffffffffu)
            indexAdd(h.info.seq, s, n);
        if (h.info.seq >= nextSeq)
            nextSeq = h.info.seq + 1;
        s += n;
    }
    callsReady = true;
}

bool callStore_ready(void)
{
    return callsReady;
}

uint16_t callStore_count(void)
{
    return nCalls;
}

uint32_t callStore_maxFrames(void)
{
    return ((CALL_SECTORS - 1) * CALL_SECTOR - CALL_HDR)
         / CALLSTORE_FRAME_BYTES;
}

bool callStore_info(uint16_t n, struct callStoreInfo *out)
{
    struct callHeader h;
    bool ok = false;

    k_mutex_lock(&callMutex, K_FOREVER);
    if (n < nCalls && readHeader(calls[n].sector, &h)) {
        *out = h.info;
        ok = true;
    }
    k_mutex_unlock(&callMutex);
    return ok;
}

int callStore_read(uint32_t seq, uint32_t first, uint8_t *buf, uint32_t count)
{
    int ret = -1;

    k_mutex_lock(&callMutex, K_FOREVER);
    int i = indexFind(seq);
    if (i >= 0) {
        struct callHeader h;
        if (readHeader(calls[i].sector, &h)) {
            if (first >= h.info.frames)
                count = 0;
            else if (first + count > h.info.frames)
                count = h.info.frames - first;
            uint32_t addr = sectorAddr(calls[i].sector) + CALL_HDR
                          + first * CALLSTORE_FRAME_BYTES;
            if (count == 0
                || nvm_devRead(&eflash, addr, buf,
                               count * CALLSTORE_FRAME_BYTES)
                       >= 0)
                ret = (int)count;
        }
    }
    k_mutex_unlock(&callMutex);
    return ret;
}

int callStore_save(struct callStoreInfo *info, const uint8_t *frames)
{
    if (!callsReady || info->frames == 0
        || info->frames > callStore_maxFrames())
        return -1;

    uint8_t n = sectorsOf(info->frames);

    /* After the newest call, or back at the start */
    k_mutex_lock(&callMutex, K_FOREVER);
    uint8_t start = 0;
    if (nCalls > 0)
        start = calls[0].sector + calls[0].sectors;
    if (start + n > CALL_SECTORS)
        start = 0;
    /* The calls in the way are dropped before anything is erased */
    for (uint16_t i = 0; i < nCalls;) {
        bool overlap = calls[i].sector < start + n
                    && calls[i].sector + calls[i].sectors > start;
        if (overlap)
            indexRemove(i);
        else
            i++;
    }
    info->seq = nextSeq++;
    k_mutex_unlock(&callMutex);

    for (uint8_t s = 0; s < n; s++) {
        k_mutex_lock(&callMutex, K_FOREVER);
        int ret = nvm_devErase(&eflash, sectorAddr(start + s), CALL_SECTOR);
        k_mutex_unlock(&callMutex);
        if (ret < 0)
            return -1;
        k_msleep(20);
    }

    uint32_t bytes = info->frames * CALLSTORE_FRAME_BYTES;
    uint32_t base = sectorAddr(start) + CALL_HDR;
    for (uint32_t off = 0; off < bytes; off += CALL_PAGE) {
        uint32_t len = MIN(CALL_PAGE, bytes - off);
        k_mutex_lock(&callMutex, K_FOREVER);
        int ret = nvm_devWrite(&eflash, base + off, frames + off, len);
        k_mutex_unlock(&callMutex);
        if (ret < 0)
            return -1;
        k_msleep(2);
    }

    /* The header last: the call is there once it is written */
    struct callHeader h = {
        .magic = CALL_MAGIC,
        .live = 0xffffffffu,
        .info = *info,
    };
    h.crc = chlist_crc32(&h.info, sizeof(h.info));
    k_mutex_lock(&callMutex, K_FOREVER);
    int ret = nvm_devWrite(&eflash, sectorAddr(start), &h, sizeof(h));
    if (ret >= 0)
        indexAdd(info->seq, start, n);
    k_mutex_unlock(&callMutex);
    return (ret < 0) ? -1 : 0;
}

int callStore_delete(uint32_t seq)
{
    int ret = -1;

    k_mutex_lock(&callMutex, K_FOREVER);
    int i = indexFind(seq);
    if (i >= 0) {
        uint32_t dead = 0;
        ret = nvm_devWrite(&eflash, sectorAddr(calls[i].sector) + 4, &dead,
                           sizeof(dead));
        if (ret >= 0) {
            indexRemove((uint16_t)i);
            ret = 0;
        }
    }
    k_mutex_unlock(&callMutex);
    return ret;
}
#endif /* CONFIG_TETRA_CALLREC */

/* CRCs of what is stored, to skip writes of unchanged data */
static uint16_t settingsCrc;
static uint16_t vfoCrc;
static bool ready;

void nvm_init()
{
    if (zephirFlash_init(&eflash) < 0)
        return;
    /*
     * eeep writes records of any length at any address, and rewrites a
     * record header to mark it valid. The devicetree gives a 4-byte write
     * block, but the CSK6 driver programs any byte range and the NOR flash
     * clears bits of programmed bytes: allow single bytes, as on the CS7000.
     */
    eflashInfo.write_size = 1;
    ready = eeep_init(&eeep, 0, 0) == 0;
#ifdef CONFIG_MEMCH
    memchLoad();
#endif
#ifdef CONFIG_TETRA_CALLREC
    callsLoad();
#endif
}

void nvm_terminate()
{
    eeep_terminate(&eeep);
}

void nvm_readCalibData(void *buf)
{
    (void)buf;
}

void nvm_readHwInfo(hwInfo_t *info)
{
    (void)info;
}

int nvm_readVfoChannelData(channel_t *channel)
{
    if (!ready)
        return -1;

    memset(channel, 0x00, sizeof(channel_t));
    if (nvm_read(1, 0, EEEP_VFO, channel, sizeof(channel_t)) < 0)
        return -1;

    vfoCrc = crc_ccitt(channel, sizeof(channel_t));
    return 0;
}

int nvm_readSettings(settings_t *settings)
{
    if (!ready)
        return -1;

    memset(settings, 0x00, sizeof(settings_t));
    if (nvm_read(1, 0, EEEP_SETTINGS, settings, sizeof(settings_t)) < 0)
        return -1;

    settingsCrc = crc_ccitt(settings, sizeof(settings_t));
    return 0;
}

int nvm_writeSettings(const settings_t *settings)
{
    if (!ready)
        return -1;

    uint16_t crc = crc_ccitt(settings, sizeof(settings_t));
    if (crc == settingsCrc)
        return 0;
    if (nvm_write(1, 0, EEEP_SETTINGS, settings, sizeof(settings_t)) < 0)
        return -1;

    settingsCrc = crc;
    return 0;
}

int nvm_writeSettingsAndVfo(const settings_t *settings, const channel_t *vfo)
{
    if (!ready)
        return -1;

    uint16_t crc = crc_ccitt(vfo, sizeof(channel_t));
    if (crc != vfoCrc) {
        if (nvm_write(1, 0, EEEP_VFO, vfo, sizeof(channel_t)) < 0)
            return -1;
        vfoCrc = crc;
    }

    return nvm_writeSettings(settings);
}
