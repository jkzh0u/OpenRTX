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
 * it also draws memory mode for every channel of that list, and with
 * TETRA_ID_LIST the names of known identities and the locked screen with
 * each of its talkgroup filters.
 *
 * Build and run with scripts/c62/ui_render.sh.
 *
 * Usage: ui_render [-n MCC-MNC] [-t GSSI-ISSI] [-s scale] output_dir
 *
 *     -n  network code shown for the example cell (default 235-78)
 *     -t  called group and talker of the example calls (default 9001-320026)
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
#include "protocols/TETRA/TetraCalls.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraStatus.h"
#include "core/settings.h"
#include "core/fast_copy.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "interfaces/platform.h"

/* Defined in ui.c, which is not built here */
const color_t color_black = { 0, 0, 0, 255 };
const color_t color_grey = { 60, 60, 60, 255 };
const color_t color_white = { 255, 255, 255, 255 };
const color_t yellow_fab413 = { 250, 180, 19, 255 };
layout_t layout;
state_t last_state;

/* _ui_calculateLayout(), copied from ui.c by scripts/c62/ui_render.sh */
#include "ui_layout.inc"

/* The menu entries, copied from ui.c by scripts/c62/ui_render.sh */
#include "ui_items.inc"

void _ui_drawMainVFO(ui_state_t *ui_state);
void _ui_drawMainMEM(ui_state_t *ui_state);
void _ui_drawTetraRecent(ui_state_t *ui_state);
void _ui_drawTetraRecentDetail(ui_state_t *ui_state);
void _ui_drawTetraFieldTest(ui_state_t *ui_state);
void _ui_drawFastCopy(ui_state_t *ui_state);

/* Menus, ui_menu.c */
void _ui_drawMenuTop(ui_state_t *ui_state);
void _ui_drawMenuSettings(ui_state_t *ui_state);
void _ui_drawSettingsDisplay(ui_state_t *ui_state);
void _ui_drawSettingsRadio(ui_state_t *ui_state);
void _ui_drawSettingsM17(ui_state_t *ui_state);
void _ui_drawSettingsFM(ui_state_t *ui_state);
void _ui_drawSettingsAccessibility(ui_state_t *ui_state);
void _ui_drawSettingsTetra(ui_state_t *ui_state);
void _ui_drawSettingsReset2Defaults(ui_state_t *ui_state);
void _ui_drawMenuInfo(ui_state_t *ui_state);
void _ui_drawMenuAbout(ui_state_t *ui_state);
bool _ui_drawMacroMenu(ui_state_t *ui_state);

static struct tetraStatus tetra;
static const long long now = 100000;
static const char *outDir;
static char outPath[512];
static int scale = 4;

/* ---- Stubs for the radio ---- */

/* Defined elsewhere in the firmware, not built here */
state_t state;
bool macro_latched;
const uint32_t freq_steps[] = { 1000,  5000,  6250,  10000, 12500,
                                15000, 20000, 25000, 50000, 100000 };

unsigned int getHeapSize()
{
    return 0;
}

unsigned int getCurrentFreeHeap()
{
    return 0;
}

const hwInfo_t *platform_getHwInfo()
{
    static const hwInfo_t info = { .name = "c62",
                                   .uhf_band = 1,
                                   .vhf_band = 1,
                                   .uhf_maxFreq = 480,
                                   .uhf_minFreq = 400,
                                   .vhf_maxFreq = 174,
                                   .vhf_minFreq = 137 };
    return &info;
}

/* No voice prompts */
void vp_flush()
{
}

void vp_play()
{
}

void vp_queuePrompt(const uint16_t prompt)
{
    (void)prompt;
}

void vp_queueStringTableEntry(const char *const *stringTableStringPtr)
{
    (void)stringTableStringPtr;
}

void vp_announceText(const char *text, const enum vpQueueFlags flags)
{
    (void)text;
    (void)flags;
}

void tetra_getStatus(struct tetraStatus *out)
{
    *out = tetra;
}

long long getTick()
{
    return now;
}

/* For roger_beep.c, which plays nothing here */
void delayMs(unsigned int mseconds)
{
    (void)mseconds;
}

void delayUs(unsigned int useconds)
{
    (void)useconds;
}

static bool pttStatus;
static struct fastCopyResult fastCopy;

void fastCopy_get(struct fastCopyResult *out)
{
    *out = fastCopy;
}

static bool fmRxOpen;

bool platform_getPttStatus()
{
    return pttStatus;
}

uint8_t platform_getMicLevel()
{
    return 0;
}

rtxStatus_t rtx_getCurrentStatus()
{
    rtxStatus_t s;
    memset(&s, 0, sizeof(s));
    s.opMode = last_state.channel.mode;
    s.opStatus = pttStatus ? TX : (fmRxOpen ? RX : OFF);
    s.rxFrequency = last_state.channel.rx_frequency;
    s.txFrequency = last_state.channel.tx_frequency;
    return s;
}

bool rtx_rxSquelchOpen()
{
    return fmRxOpen;
}

const uint16_t ctcss_tone[] = { 670 };

/* As in core/cps.c */
const uint16_t dcs_code[DCS_CODE_NUM] = {
    0023, 0025, 0026, 0031, 0032, 0036, 0043, 0047, 0051, 0053, 0054, 0065,
    0071, 0072, 0073, 0074, 0114, 0115, 0116, 0122, 0125, 0131, 0132, 0134,
    0143, 0145, 0152, 0155, 0156, 0162, 0165, 0172, 0174, 0205, 0212, 0223,
    0225, 0226, 0243, 0244, 0245, 0246, 0251, 0252, 0255, 0261, 0263, 0265,
    0266, 0271, 0274, 0306, 0311, 0315, 0325, 0331, 0332, 0343, 0346, 0351,
    0356, 0364, 0365, 0371, 0411, 0412, 0413, 0423, 0431, 0432, 0445, 0446,
    0452, 0454, 0455, 0462, 0464, 0465, 0466, 0503, 0506, 0516, 0523, 0526,
    0532, 0546, 0565, 0606, 0612, 0624, 0627, 0631, 0632, 0654, 0662, 0664,
    0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754
};

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

enum screen {
    SCREEN_VFO,
    SCREEN_MEM,
    SCREEN_RECENT,
    SCREEN_DETAIL,
    SCREEN_FIELD_TEST,
    SCREEN_FAST_COPY
};

/* Memory channel number being typed, drawn on the next scene */
static uint16_t chEntry;
/* First line shown of a scrolling screen */
static uint8_t scroll;

static void draw(const char *name, enum screen screen)
{
    static ui_state_t ui;

    snprintf(outPath, sizeof(outPath), "%s/%s.png", outDir, name);
    memset(&ui, 0, sizeof(ui));
    ui.ch_entry = chEntry;
    ui.ch_entry_digits = (chEntry > 0) ? 1 : 0;
    ui.menu_selected = scroll;
    if (screen == SCREEN_MEM)
        _ui_drawMainMEM(&ui);
    else if (screen == SCREEN_RECENT)
        _ui_drawTetraRecent(&ui);
    else if (screen == SCREEN_DETAIL)
        _ui_drawTetraRecentDetail(&ui);
    else if (screen == SCREEN_FIELD_TEST)
        _ui_drawTetraFieldTest(&ui);
    else if (screen == SCREEN_FAST_COPY)
        _ui_drawFastCopy(&ui);
    else
        _ui_drawMainVFO(&ui);
    gfx_render();
    printf("%s\n", outPath);
}

static void scene(const char *name, bool memoryMode)
{
    draw(name, memoryMode ? SCREEN_MEM : SCREEN_VFO);
}

static void tetraScenes(unsigned mcc, unsigned mnc, uint32_t group,
                        uint32_t talker)
{
    uint32_t cellFreq = 392512500, prevFreq = 392612500;
    channel_t ch;

    strcpy(last_state.channel.name, "TETRA 392");
    last_state.channel_index = 4;

    /*
     * With a channel list, its first TETRA channel is the example cell, and
     * the next one of another station the cell before it
     */
    bool found = false;
    for (uint16_t i = 0; cps_readChannel(&ch, i) == 0; i++) {
        if (ch.mode != OPMODE_TETRA)
            continue;
        if (!found) {
            cellFreq = ch.rx_frequency;
            strcpy(last_state.channel.name, ch.name);
            last_state.channel_index = i;
            found = true;
        } else if (strncmp(ch.name, last_state.channel.name, 4) != 0) {
            prevFreq = ch.rx_frequency;
            break;
        }
    }

    last_state.channel.mode = OPMODE_TETRA;
    last_state.channel.rx_frequency = cellFreq;
    last_state.channel.tx_frequency = cellFreq;

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
    tetra.dlFreq = cellFreq;
    tetra.phaseErr = 12;
    tetra.carrierHz = -234;
    scene("tetra-2-cell", false);
    scene("tetra-2b-cell-memory", true);

    /* A memory channel number being typed */
    chEntry = 12;
    scene("tetra-2c-channel-entry", true);
    chEntry = 0;

    /* Searching for the best signal (long 0), still on the cell */
    tetra.searching = true;
    scene("tetra-2d-best-signal", false);
    tetra.searching = false;

    /* Field test: neighbours, a measurement, network time; then scrolled */
    tetra.tunedHz = cellFreq;
    tetra.load = 1;
    tetra.resyncs = 2;
    tetra.time = true;
    tetra.timeLocal = true;
    tetra.timeOfDay = 8 * 3600 + 14 * 60 + 5;
    tetra.timeTick = now - 30000;
    tetra.roamState = 0;
    tetra.nNeighbours = 3;
    tetra.neighbour[0] = (struct tetraStatusNeighbour){ prevFreq, 3801, 2, 2,
                                                        false };
    tetra.neighbour[1] = (struct tetraStatusNeighbour){ cellFreq + 50000, 3805,
                                                        3, 1, false };
    tetra.neighbour[2] = (struct tetraStatusNeighbour){ 0, 1234, 0xffff, 0,
                                                        true };
    tetra.nMeasured = 3;
    tetra.measured[0] = (struct tetraStatusMeasured){ cellFreq, -84 };
    tetra.measured[1] = (struct tetraStatusMeasured){ prevFreq, -97 };
    tetra.measured[2] = (struct tetraStatusMeasured){ cellFreq + 50000, -109 };
    tetra.measuredTime = now - 12000;
    draw("tetra-15-field-test", SCREEN_FIELD_TEST);
    scroll = 7;
    draw("tetra-15a-field-test-middle", SCREEN_FIELD_TEST);
    scroll = 255;
    draw("tetra-15b-field-test-end", SCREEN_FIELD_TEST);
    scroll = 0;
    tetra.nNeighbours = 0;
    tetra.nMeasured = 0;
    tetra.time = false;
    tetra.roamState = 0xff;

    /* Last SDS text, from the example talker */
    tetra.sdsSrc = talker;
    strcpy(tetra.sds, "Test message from dispatch");
    tetra.sdsTime = now - 1000;
    scene("tetra-3-sds", false);
    tetra.sdsSrc = 0;

    /* Group call on the main carrier */
    tetra.call = true;
    tetra.callId = 8;
    tetra.callGroup = true;
    tetra.callDst = group;
    tetra.talker = talker;
    tetra.voice = true;
    tetra.txStart = now - 12000;
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

    /* Last heard on line 1, after the call */
    tetra.call = false;
    tetra.callEncrypted = false;
    tetraCalls_init(&tetra.recent);
    tetraCalls_note(&tetra.recent, 101011, 0, true, true, false,
                    now - 47 * 60000);
    tetraCalls_note(&tetra.recent, 100020, 100253, true, true, false,
                    now - 9 * 60000);
    tetraCalls_note(&tetra.recent, 100112, 0, false, true, false,
                    now - 4 * 60000);
    tetraCalls_note(&tetra.recent, 102716, 0, true, false, true,
                    now - 3 * 60000);
    tetraCalls_note(&tetra.recent, 60002, 0, true, true, false, now - 61000);
    tetraCalls_note(&tetra.recent, group, talker, true, true, false,
                    now - 14 * 60000);
    tetraCalls_note(&tetra.recent, group, 100253, true, true, false,
                    now - 95000);
    scene("tetra-7-last-heard", false);
    draw("tetra-8-recent", SCREEN_RECENT);
    draw("tetra-8b-recent-detail", SCREEN_DETAIL);

    /* Just moved to another cell */
    tetra.cellChanged = now - 1000;
    tetra.prevLa = 2;
    tetra.prevDlFreq = prevFreq;
    scene("tetra-12-moved", false);
    tetra.cellChanged = 0;

    /* A weak signal, still decoding; then decoding badly */
    last_state.rssi = -113;
    tetra.phaseErr = 26;
    scene("tetra-13-weak", false);
    last_state.rssi = -103;
    tetra.phaseErr = 38;
    scene("tetra-14-poor-decoding", false);
    last_state.rssi = -85;
    tetra.phaseErr = 12;

    /* Holding the last talkgroup heard */
    tetraIds_hold(group);
    scene("tetra-9-hold", false);
    tetraIds_release();

    /* An alarm call */
    tetra.call = true;
    tetra.callId = 40;
    tetra.callGroup = true;
    tetra.callDst = 60002;
    tetra.talker = 0;
    tetra.voice = true;
    tetra.txStart = now - 3000;
    tetra.callAlarm = true;
    scene("tetra-10-alarm", false);
    tetra.call = false;
    tetra.callAlarm = false;

    /* Lost: scanning the network's carriers */
    tetra.sync = false;
    tetra.searching = true;
    tetra.phaseErr = 61;
    tetra.carrierHz = -3020;
    scene("tetra-11-scanning", false);
    tetra.sync = true;
    tetra.searching = false;
    tetra.phaseErr = 12;
    tetra.carrierHz = -234;
}

/**
 * FM: idle, receiving, with tones and a repeater shift, transmitting.
 */
static void fmScenes(void)
{
    channel_t saved = last_state.channel;
    char savedName[sizeof(last_state.channel.name)];

    strcpy(savedName, last_state.channel.name);
    last_state.channel.mode = OPMODE_FM;
    last_state.channel.rx_frequency = 146520000;
    last_state.channel.tx_frequency = 146520000;
    last_state.channel.bandwidth = BW_25;
    last_state.channel.power = 5000;
    last_state.channel.fm.rxToneEn = 0;
    last_state.channel.fm.txToneEn = 0;
    strcpy(last_state.channel.name, "Calling 2m");
    last_state.channel_index = 2;
    last_state.settings.sqlLevel = 4;
    last_state.step_index = 4; /* 12.5 kHz, as at first start */
    last_state.rssi = -127;
    scene("fm-1-idle", false);
    scene("fm-1b-idle-memory", true);

    last_state.rssi = -72;
    fmRxOpen = true;
    scene("fm-2-receiving", false);

    /* A repeater: shift and tones */
    last_state.channel.rx_frequency = 443150000;
    last_state.channel.tx_frequency = 448150000;
    last_state.channel.bandwidth = BW_12_5;
    last_state.channel.fm.txToneEn = 1;
    last_state.channel.fm.rxToneEn = 1;
    last_state.channel.fm.txTone = 0;
    last_state.channel.fm.rxTone = 0;
    last_state.channel.power = 1000;
    strcpy(last_state.channel.name, "Repeater");
    last_state.rssi = -101;
    scene("fm-3-repeater-memory", true);
    fmRxOpen = false;

    pttStatus = true;
    scene("fm-4-transmitting", true);
    pttStatus = false;

    /* A DCS channel: D023N both ways */
    last_state.channel.rxDcs = 1;
    last_state.channel.txDcs = 1;
    last_state.channel.fm.txTone = 0;
    last_state.channel.fm.rxTone = 0;
    last_state.rssi = -127;
    strcpy(last_state.channel.name, "GMRS DCS");
    last_state.channel.rx_frequency = 462562500;
    last_state.channel.tx_frequency = 462562500;
    scene("fm-5-dcs-memory", true);
    last_state.channel.rxDcs = 0;
    last_state.channel.txDcs = 0;

    /* Fast copy: listening, frequency found, found with a tone or code */
    memset(&fastCopy, 0, sizeof(fastCopy));
    fastCopy.state = FAST_COPY_FREQ;
    draw("fm-6-fast-copy-listening", SCREEN_FAST_COPY);
    fastCopy.state = FAST_COPY_TONE;
    fastCopy.hz = 446006180;
    draw("fm-6b-fast-copy-frequency", SCREEN_FAST_COPY);
    fastCopy.state = FAST_COPY_FOUND;
    fastCopy.toneFound = true;
    fastCopy.tone = 885;
    draw("fm-6c-fast-copy-ctcss", SCREEN_FAST_COPY);
    fastCopy.tone = RTX_TONE_DCS | 0023;
    draw("fm-6d-fast-copy-dcs", SCREEN_FAST_COPY);
    memset(&fastCopy, 0, sizeof(fastCopy));
    last_state.channel = saved;
    strcpy(last_state.channel.name, savedName);
    last_state.rssi = -85;
}

/* ---- Menus ---- */

typedef void (*menuDraw)(ui_state_t *ui_state);

static void menu(const char *name, menuDraw fn, uint8_t selected)
{
    static ui_state_t ui;

    snprintf(outPath, sizeof(outPath), "%s/%s.png", outDir, name);
    memset(&ui, 0, sizeof(ui));
    ui.menu_selected = selected;
    fn(&ui);
    gfx_render();
    printf("%s\n", outPath);
}

/**
 * Side key shortcuts: ui.c covers the screen in black (_ui_drawDarkOverlay)
 * and draws them on top
 */
static void macroMenu(ui_state_t *ui_state)
{
    gfx_clearScreen();
    _ui_drawMacroMenu(ui_state);
}

/**
 * The menu and the settings pages, with the default settings (the values
 * after the first power on).
 */
static void menuScenes(void)
{
    state.settings = default_settings;
    last_state.settings = default_settings;
    state.channel = last_state.channel;
    last_state.v_bat = 7600;
    state.step_index = last_state.step_index = 4; /* 12.5 kHz, as at start */

    menu("menu-01-top", _ui_drawMenuTop, 0);
    menu("menu-02-top-field-test", _ui_drawMenuTop, 5);
    menu("menu-03-settings", _ui_drawMenuSettings, 0);
    menu("menu-04-display", _ui_drawSettingsDisplay, 0);
    menu("menu-05-radio", _ui_drawSettingsRadio, 0);
    menu("menu-06-m17", _ui_drawSettingsM17, 0);
    /* FM settings on a repeater channel with a tone */
    channel_t before = last_state.channel;
    last_state.channel.mode = OPMODE_FM;
    last_state.channel.bandwidth = BW_12_5;
    last_state.channel.power = 2500;
    last_state.channel.fm.txToneEn = 1;
    last_state.channel.fm.rxToneEn = 0;
    menu("menu-07-fm", _ui_drawSettingsFM, 0);
    menu("menu-07b-fm-end", _ui_drawSettingsFM, settings_fm_num - 1);
    last_state.channel.rxDcs = 1;
    last_state.channel.rxDcsInv = 1;
    last_state.channel.fm.rxTone = 103;
    menu("menu-07c-fm-dcs", _ui_drawSettingsFM, 5);
    last_state.channel = before;
    menu("menu-08-accessibility", _ui_drawSettingsAccessibility, 0);
    menu("menu-08b-tetra", _ui_drawSettingsTetra, 0);
    menu("menu-09-reset", _ui_drawSettingsReset2Defaults, 0);
    menu("menu-10-info", _ui_drawMenuInfo, 0);
    menu("menu-11-about", _ui_drawMenuAbout, 0);
    menu("menu-12-shortcuts", macroMenu, 0);

    /* The same in FM, where the tone, width and squelch keys work */
    channel_t saved = last_state.channel;
    last_state.channel.mode = OPMODE_FM;
    last_state.channel.bandwidth = BW_12_5;
    last_state.channel.power = 5000;
    last_state.channel.fm.txToneEn = 1;
    last_state.channel.fm.rxToneEn = 1;
    last_state.channel.fm.txTone = 0; /* the only tone of this file */
    menu("menu-12b-shortcuts-fm", macroMenu, 0);
    last_state.channel = saved;
}

/**
 * The locked screen with each talkgroup filter of the list selected, as #
 * steps through them.
 */
static void filterScenes(void)
{
    char name[64];

    tetra.call = false;
    for (size_t i = 0; i < tetraIds_filterCount(); i++) {
        int n = snprintf(name, sizeof(name), "filter%02zu-", i);
        for (const char *c = tetraIds_filterName(i); *c != '\0' && n < 60; c++)
            name[n++] = (*c == ' ' || *c == '/') ? '_' : *c;
        name[n] = '\0';
        scene(name, false);
        tetraIds_stepFilter(1);
    }
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
    unsigned long group = 9001, talker = 320026;
    int opt;

    while ((opt = getopt(argc, argv, "n:s:t:")) != -1) {
        switch (opt) {
            case 'n':
                if (sscanf(optarg, "%u-%u", &mcc, &mnc) != 2) {
                    fprintf(stderr, "-n: expected MCC-MNC, e.g. 235-78\n");
                    return 2;
                }
                break;
            case 't':
                if (sscanf(optarg, "%lu-%lu", &group, &talker) != 2) {
                    fprintf(stderr,
                            "-t: expected GSSI-ISSI, e.g. 9001-320026\n");
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
                        "usage: %s [-n MCC-MNC] [-t GSSI-ISSI] [-s scale] "
                        "output_dir\n",
                        argv[0]);
                return 2;
        }
    }
    if (optind != argc - 1) {
        fprintf(stderr,
                "usage: %s [-n MCC-MNC] [-t GSSI-ISSI] [-s scale] "
                "output_dir\n",
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

    tetraScenes(mcc, mnc, (uint32_t)group, (uint32_t)talker);
    fmScenes();
    menuScenes();
    if (tetraIds_filterCount() > 1)
        filterScenes();
#ifdef CPS_BUILTIN_LIST
    channelScenes();
#endif
    return 0;
}
