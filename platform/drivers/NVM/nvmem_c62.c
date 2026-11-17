/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Retevis C62 settings storage: the settings and the current channel, kept
 * with Zephyr's NVS in the first sectors of storage_partition (0x2b0000, a
 * flash area found erased in every factory dump examined, see c62.dts).
 *
 * NVS writes an item only when it changed, and a write cut short by a power
 * loss leaves the previous value readable. Writes happen at power off
 * (state_terminate()): flash writes and erases run with interrupts masked,
 * up to tens of milliseconds for an erase, which would break audio while
 * receiving.
 *
 * The C62 has no codeplug or calibration in flash for OpenRTX: those
 * functions do nothing.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include "interfaces/nvmem.h"
#include "rtx/rtx.h"

/* Sectors of storage_partition used, of 4 kB each */
#define NVS_SECTORS 4

/* Item identifiers */
enum {
    ID_FORMAT = 1,   /**< Layout of the items below */
    ID_SETTINGS = 2, /**< settings_t */
    ID_VFO = 3,      /**< channel_t of the channel in use */
};

/*
 * Layout of the stored items: changes when settings_t or channel_t change
 * size, or when FORMAT_VERSION is raised for a change of meaning. Items of
 * another layout are not used, and are replaced at the next power off.
 */
#define FORMAT_VERSION 1
#define FORMAT                                                     \
    (((uint32_t)FORMAT_VERSION << 24) | ((uint32_t)sizeof(settings_t) << 12) \
     | (uint32_t)sizeof(channel_t))

static struct nvs_fs fs;
static bool mounted;
static bool failed;

/**
 * Mount the store on first use. Flash that holds something else than an NVS
 * store is erased: only the NVS_SECTORS sectors at the start of
 * storage_partition.
 *
 * @return true if the store can be used.
 */
static bool mount(void)
{
    struct flash_pages_info page;

    if (mounted)
        return true;
    if (failed)
        return false;

    fs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
    fs.offset = FIXED_PARTITION_OFFSET(storage_partition);
    if (!device_is_ready(fs.flash_device)
        || flash_get_page_info_by_offs(fs.flash_device, fs.offset, &page)
               != 0) {
        printk("nvmem: flash not ready\n");
        failed = true;
        return false;
    }
    fs.sector_size = page.size;
    fs.sector_count = NVS_SECTORS;
    BUILD_ASSERT(NVS_SECTORS * 4096 <= FIXED_PARTITION_SIZE(storage_partition),
                 "NVS sectors do not fit storage_partition");

    int ret = nvs_mount(&fs);
    if (ret != 0) {
        printk("nvmem: mount failed (%d), erasing the settings area\n", ret);
        ret = flash_erase(fs.flash_device, fs.offset,
                          (size_t)fs.sector_size * fs.sector_count);
        if (ret == 0)
            ret = nvs_mount(&fs);
    }
    if (ret != 0) {
        printk("nvmem: no settings storage (%d)\n", ret);
        failed = true;
        return false;
    }

    mounted = true;
    return true;
}

/**
 * Read an item of exactly the expected size, from a store of this layout.
 */
static int readItem(uint16_t id, void *data, size_t len)
{
    uint32_t format;

    if (!mount())
        return -1;
    if (nvs_read(&fs, ID_FORMAT, &format, sizeof(format)) != sizeof(format)
        || format != FORMAT)
        return -1;
    if (nvs_read(&fs, id, data, len) != (ssize_t)len)
        return -1;

    return 0;
}

/**
 * @return true if a stored channel can be used.
 */
static bool channelValid(const channel_t *ch)
{
    switch (ch->mode) {
        case OPMODE_FM:
        case OPMODE_M17:
#ifdef CONFIG_TETRA
        case OPMODE_TETRA:
#endif
            break;
        default:
            return false;
    }

    return ch->rx_frequency != 0;
}

void nvm_init()
{
    mount();
}

void nvm_terminate()
{
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
    channel_t ch;

    if (readItem(ID_VFO, &ch, sizeof(ch)) < 0 || !channelValid(&ch))
        return -1;

    *channel = ch;
    return 0;
}

int nvm_readSettings(settings_t *settings)
{
    return readItem(ID_SETTINGS, settings, sizeof(*settings));
}

static int writeItem(uint16_t id, const void *data, size_t len)
{
    /* NVS returns 0 when the stored value is the same: nothing written */
    ssize_t ret = nvs_write(&fs, id, data, len);
    return (ret < 0) ? -1 : 0;
}

static int writeFormat(void)
{
    const uint32_t format = FORMAT;
    return writeItem(ID_FORMAT, &format, sizeof(format));
}

int nvm_writeSettings(const settings_t *settings)
{
    if (!mount())
        return -1;
    if (writeFormat() < 0)
        return -1;

    return writeItem(ID_SETTINGS, settings, sizeof(*settings));
}

int nvm_writeSettingsAndVfo(const settings_t *settings, const channel_t *vfo)
{
    if (nvm_writeSettings(settings) < 0)
        return -1;

    return writeItem(ID_VFO, vfo, sizeof(*vfo));
}
