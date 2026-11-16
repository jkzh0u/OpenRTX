/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * UI render tool: draws the main screen with the firmware's own code
 * (openrtx/src/ui/default/ui_main.c, core/graphics.c, its fonts and the
 * layout from ui.c) for a set of receiver states, and writes each frame as
 * a PNG. Radio state and TETRA status are stubbed with example values.
 * Previews UI changes without a radio.
 *
 * The display settings come from hwconfig.h next to this file (the C62's).
 * With CPS_BUILTIN_LIST set, as for platform/drivers/CPS/cps_io_builtin.c,
 * it also draws memory mode for every channel of that list.
 *
 * Build and run with scripts/c62/ui_render.sh.
 *
 * Usage: ui_render [-n MCC-MNC] [-s scale] output_dir
 *
 *     -n  network code shown for the example cell (default 235-78)
 *     -s  pixel scale of the PNG files (default 4)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include "hwconfig.h"
#include "ui/ui_default.h"
#include "rtx/rtx.h"
#include "core/graphics.h"
#include "interfaces/display.h"
#include "interfaces/cps_io.h"
#include "protocols/TETRA/TetraStatus.h"

/* Defined in ui.c, which is not built here */
const color_t color_black = { 0, 0, 0, 255 };
const color_t color_grey = { 60, 60, 60, 255 };
const color_t color_white = { 255, 255, 255, 255 };
const color_t yellow_fab413 = { 250, 180, 19, 255 };
layout_t layout;
state_t last_state;

/* _ui_calculateLayout(), copied from ui.c by scripts/c62/ui_render.sh */
#include "ui_layout.inc"

void _ui_drawMainVFO(ui_state_t *ui_state);
void _ui_drawMainMEM(ui_state_t *ui_state);

static struct tetraStatus tetra;
static const long long now = 100000;
static const char *outDir;
static char outPath[512];
static int scale = 4;

/* ---- Stubs for the radio ---- */

void tetra_getStatus(struct tetraStatus *out)
{
    *out = tetra;
}

long long getTick()
{
    return now;
}

bool platform_getPttStatus()
{
    return false;
}

uint8_t platform_getMicLevel()
{
    return 0;
}

rtxStatus_t rtx_getCurrentStatus()
{
    rtxStatus_t s;
    memset(&s, 0, sizeof(s));
    return s;
}

const uint16_t ctcss_tone[] = { 670 };

/* ---- Display: the frame buffer to a PNG file ---- */

void display_init()
{
}

void display_terminate()
{
}

void display_setContrast(uint8_t contrast)
{
    (void)contrast;
}

void display_setBacklightLevel(uint8_t level)
{
    (void)level;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void pngChunk(FILE *f, const char *type, const uint8_t *data,
                     uint32_t len)
{
    uint8_t b[4];
    uLong crc = crc32(0, (const Bytef *)type, 4);

    put32(b, len);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len != 0) {
        fwrite(data, 1, len, f);
        crc = crc32(crc, data, len);
    }
    put32(b, (uint32_t)crc);
    fwrite(b, 1, 4, f);
}

void display_render(void *fb)
{
    static const uint8_t signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    const uint16_t *px = fb;
    uint32_t w = CONFIG_SCREEN_WIDTH * scale;
    uint32_t h = CONFIG_SCREEN_HEIGHT * scale;
    size_t rowLen = 1 + 3 * (size_t)w;
    uint8_t *raw = malloc(rowLen * h);
    uLongf zlen = compressBound(rowLen * h);
    uint8_t *z = malloc(zlen);
    uint8_t ihdr[13];

    /* RGB565 to RGB888, each pixel scale x scale */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + rowLen * y;
        row[0] = 0; /* no filter */
        for (uint32_t x = 0; x < w; x++) {
            uint16_t p = px[(y / scale) * CONFIG_SCREEN_WIDTH + x / scale];
            uint8_t r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
            row[1 + 3 * x] = (r << 3) | (r >> 2);
            row[2 + 3 * x] = (g << 2) | (g >> 4);
            row[3 + 3 * x] = (b << 3) | (b >> 2);
        }
    }
    compress2(z, &zlen, raw, rowLen * h, 9);

    FILE *f = fopen(outPath, "wb");
    if (f == NULL) {
        perror(outPath);
        exit(1);
    }
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8;  /* bits per sample */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filtering */
    ihdr[12] = 0; /* no interlace */
    fwrite(signature, 1, 8, f);
    pngChunk(f, "IHDR", ihdr, 13);
    pngChunk(f, "IDAT", z, (uint32_t)zlen);
    pngChunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

void display_renderRows(uint8_t startRow, uint8_t endRow, void *fb)
{
    (void)startRow;
    (void)endRow;
    display_render(fb);
}

/* ---- Scenes ---- */

static void scene(const char *name, bool memoryMode)
{
    static ui_state_t ui;

    snprintf(outPath, sizeof(outPath), "%s/%s.png", outDir, name);
    memset(&ui, 0, sizeof(ui));
    if (memoryMode)
        _ui_drawMainMEM(&ui);
    else
        _ui_drawMainVFO(&ui);
    gfx_render();
    printf("%s\n", outPath);
}

static void tetraScenes(unsigned mcc, unsigned mnc)
{
    last_state.channel.mode = OPMODE_TETRA;
    last_state.channel.rx_frequency = 392512500;
    last_state.channel.tx_frequency = 392512500;
    strcpy(last_state.channel.name, "TETRA 392");
    last_state.channel_index = 4;

    /* No signal: noise gives about 58% phase error */
    memset(&tetra, 0, sizeof(tetra));
    tetra.phaseErr = 63;
    tetra.carrierHz = -1180;
    tetra.resyncs = 1;
    last_state.rssi = -127;
    scene("tetra-1-searching", false);

    /* Locked to a cell */
    last_state.rssi = -85;
    tetra.sync = true;
    tetra.cell = true;
    tetra.sysinfo = true;
    tetra.mcc = mcc;
    tetra.mnc = mnc;
    tetra.la = 1;
    tetra.phaseErr = 12;
    tetra.carrierHz = -234;
    scene("tetra-2-cell", false);
    scene("tetra-2b-cell-memory", true);

    /* Last SDS text */
    tetra.sdsSrc = 320026;
    strcpy(tetra.sds, "Test message from dispatch");
    tetra.sdsTime = now - 1000;
    scene("tetra-3-sds", false);
    tetra.sdsSrc = 0;

    /* Group call on the main carrier */
    tetra.call = true;
    tetra.callId = 8;
    tetra.callGroup = true;
    tetra.callDst = 9001;
    tetra.talker = 320026;
    tetra.voice = true;
    scene("tetra-4-call", false);

    /* Call followed to another carrier */
    tetra.callId = 21;
    tetra.followHz = 392562500;
    scene("tetra-5-call-followed", false);

    /* Encrypted call, talker not known */
    tetra.followHz = 0;
    tetra.callId = 30;
    tetra.talker = 0;
    tetra.voice = false;
    tetra.callEncrypted = true;
    scene("tetra-6-call-encrypted", false);
}

#ifdef CPS_BUILTIN_LIST
/**
 * Memory mode on every channel of the built-in list, as it shows right
 * after selecting the channel: TETRA still searching for the cell.
 */
static void channelScenes(void)
{
    channel_t ch;
    char name[64];

    memset(&tetra, 0, sizeof(tetra));
    last_state.rssi = -127;

    for (uint16_t i = 0; cps_readChannel(&ch, i) == 0; i++) {
        last_state.channel = ch;
        last_state.channel_index = i;

        /* File name from the channel name, e.g. ch001-CN_Tower_1 */
        int n = snprintf(name, sizeof(name), "ch%03u-", i + 1);
        for (const char *c = ch.name; *c != '\0' && n < 60; c++)
            name[n++] = (*c == ' ' || *c == '/') ? '_' : *c;
        name[n] = '\0';
        scene(name, true);
    }
}
#endif

int main(int argc, char *argv[])
{
    unsigned mcc = 235, mnc = 78;
    int opt;

    while ((opt = getopt(argc, argv, "n:s:")) != -1) {
        switch (opt) {
            case 'n':
                if (sscanf(optarg, "%u-%u", &mcc, &mnc) != 2) {
                    fprintf(stderr, "-n: expected MCC-MNC, e.g. 235-78\n");
                    return 2;
                }
                break;
            case 's':
                scale = atoi(optarg);
                if (scale < 1 || scale > 16) {
                    fprintf(stderr, "-s: scale from 1 to 16\n");
                    return 2;
                }
                break;
            default:
                fprintf(stderr,
                        "usage: %s [-n MCC-MNC] [-s scale] output_dir\n",
                        argv[0]);
                return 2;
        }
    }
    if (optind != argc - 1) {
        fprintf(stderr, "usage: %s [-n MCC-MNC] [-s scale] output_dir\n",
                argv[0]);
        return 2;
    }
    outDir = argv[optind];

    gfx_init();
    _ui_calculateLayout(&layout);

    /* Default settings: battery as a percentage */
    memset(&last_state, 0, sizeof(last_state));
    last_state.charge = 80;
    last_state.volume = 180;
    last_state.settings.showBatteryIcon = false;

    tetraScenes(mcc, mnc);
#ifdef CPS_BUILTIN_LIST
    channelScenes();
#endif
    return 0;
}
