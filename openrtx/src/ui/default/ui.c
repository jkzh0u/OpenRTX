/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The graphical user interface (GUI) works by splitting the screen in
 * horizontal rows, with row height depending on vertical resolution.
 *
 * The general screen layout is composed by an upper status bar at the
 * top of the screen and a lower status bar at the bottom.
 * The central portion of the screen is filled by two big text/number rows
 * And a small row.
 *
 * Below is shown the row height for two common display densities.
 *
 *        160x128 display (MD380)            Recommended font size
 *      ┌─────────────────────────┐
 *      │  top_status_bar (16px)  │  8 pt (11 px) font with 2 px vertical padding
 *      │      top_pad (4px)      │  4 px padding
 *      │      Line 1 (20px)      │  8 pt (11 px) font with 4 px vertical padding
 *      │      Line 2 (20px)      │  8 pt (11 px) font with 4 px vertical padding
 *      │                         │
 *      │      Line 3 (40px)      │  16 pt (xx px) font with 6 px vertical padding
 *      │ RSSI+squelch bar (20px) │  20 px
 *      │      bottom_pad (4px)   │  4 px padding
 *      └─────────────────────────┘
 *
 *         128x64 display (GD-77)
 *      ┌─────────────────────────┐
 *      │  top_status_bar (11 px) │  6 pt (9 px) font with 1 px vertical padding
 *      │      top_pad (1px)      │  1 px padding
 *      │      Line 1 (10px)      │  6 pt (9 px) font without vertical padding
 *      │      Line 2 (10px)      │  6 pt (9 px) font with 2 px vertical padding
 *      │      Line 3 (18px)      │  12 pt (xx px) font with 0 px vertical padding
 *      │ RSSI+squelch bar (11px) │  11 px
 *      │      bottom_pad (1px)   │  1 px padding
 *      └─────────────────────────┘
 *
 *         128x48 display (RD-5R)
 *      ┌─────────────────────────┐
 *      │  top_status_bar (11 px) │  6 pt (9 px) font with 1 px vertical padding
 *      ├─────────────────────────┤  1 px line
 *      │      Line 2 (10px)      │  8 pt (11 px) font with 4 px vertical padding
 *      │      Line 3 (18px)      │  8 pt (11 px) font with 4 px vertical padding
 *      └─────────────────────────┘
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "ui/ui_default.h"
#include "rtx/rtx.h"
#include "interfaces/platform.h"
#include "interfaces/display.h"
#include "interfaces/cps_io.h"
#include "interfaces/nvmem.h"
#include "interfaces/delays.h"
#include <string.h>
#include "core/battery.h"
#include "core/input.h"
#include "core/utils.h"
#include "hwconfig.h"
#include "core/voicePromptUtils.h"
#include "core/beeps.h"
#include "core/roger_beep.h"
#include "core/vox.h"
#include "core/bcast.h"
#include "core/dtmf.h"
#include "core/replay.h"
#include "interfaces/audio.h"
#include "protocols/TETRA/TetraCallRec.h"
#ifdef CONFIG_FAST_COPY
#include "core/fast_copy.h"
#endif
#ifdef CONFIG_SPECTRUM
#include "core/spectrum.h"
#endif
#ifdef CONFIG_MEMCH
#include "core/memch.h"
#endif
#ifdef CONFIG_TETRA
#include "protocols/TETRA/TetraConfig.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraStatus.h"
#ifdef CONFIG_TETRA_LOG
#include "protocols/TETRA/TetraLog.h"
#endif
#endif

/* UI main screen functions, their implementation is in "ui_main.c" */
extern void _ui_drawMainBackground();
extern void _ui_drawMainTop(ui_state_t* ui_state);
extern void _ui_drawVFOMiddle();
extern void _ui_drawMEMMiddle();
extern void _ui_drawVFOBottom();
extern void _ui_drawMEMBottom();
extern void _ui_drawMainVFO(ui_state_t* ui_state);
extern void _ui_drawMainVFOInput(ui_state_t* ui_state);
extern void _ui_drawMainMEM(ui_state_t* ui_state);
/* UI menu functions, their implementation is in "ui_menu.c" */
extern void _ui_drawMenuTop(ui_state_t* ui_state);
extern void _ui_drawMenuBank(ui_state_t* ui_state);
extern void _ui_drawMenuChannel(ui_state_t* ui_state);
extern void _ui_drawMenuContacts(ui_state_t* ui_state);
#ifdef CONFIG_GPS
extern void _ui_drawMenuGPS();
extern void _ui_drawSettingsGPS(ui_state_t* ui_state);
#endif
extern void _ui_drawSettingsAccessibility(ui_state_t* ui_state);
extern void _ui_drawMenuSettings(ui_state_t* ui_state);
extern void _ui_drawMenuBackupRestore(ui_state_t* ui_state);
extern void _ui_drawMenuBackup(ui_state_t* ui_state);
extern void _ui_drawMenuRestore(ui_state_t* ui_state);
extern void _ui_drawMenuInfo(ui_state_t* ui_state);
extern void _ui_drawMenuAbout(ui_state_t* ui_state);
#ifdef CONFIG_RTC
extern void _ui_drawSettingsTimeDate();
extern void _ui_drawSettingsTimeDateSet(ui_state_t* ui_state);
#endif
extern void _ui_drawSettingsDisplay(ui_state_t* ui_state);
extern void _ui_drawSettingsM17(ui_state_t* ui_state);
extern void _ui_drawSettingsFM(ui_state_t* ui_state);
extern void _ui_drawSettingsVoicePrompts(ui_state_t* ui_state);
extern void _ui_drawSettingsReset2Defaults(ui_state_t* ui_state);
extern void _ui_drawSettingsRadio(ui_state_t* ui_state);
extern bool _ui_drawMacroMenu(ui_state_t* ui_state);
extern void _ui_reset_menu_anouncement_tracking();
// TODO: get these from ui strings / currentLanguage
const char *menu_items[] =
{
    "Banks",
    "Channels",
    "Contacts",
#ifdef CONFIG_GPS
    "GPS",
#endif
#ifdef CONFIG_BCAST_FM
    "FM radio",
#endif
#ifdef CONFIG_TETRA_CALLREC
    "Saved calls",
#endif
#ifdef CONFIG_TETRA
    "Messages",
    "Activity",
#endif
    "Settings",
    "Info",
#ifdef CONFIG_SELFTEST
    "Self test",
#endif
#ifdef CONFIG_TETRA
    "Field test",
#endif
    "About"
};

const char *settings_items[] =
{
    "Display",
#ifdef CONFIG_RTC
    "Time & Date",
#endif
#ifdef CONFIG_GPS
    "GPS",
#endif
    "Radio",
#ifdef CONFIG_M17
    "M17",
#endif
    "FM",
#ifdef CONFIG_MEMCH
    "Memory",
#endif
#ifdef CONFIG_TETRA
    "TETRA",
#endif
    "Accessibility",
    "Default Settings"
};

const char *display_items[] =
{
#ifdef CONFIG_SCREEN_BRIGHTNESS
    "Brightness",
#endif
#ifdef CONFIG_SCREEN_CONTRAST
    "Contrast",
#endif
    "Timer",
    "Battery Icon"
};

#ifdef CONFIG_GPS
const char *settings_gps_items[] =
{
    "GPS Enabled",
#ifdef CONFIG_RTC
    "GPS Set Time",
    "UTC Timezone"
#endif
};
#endif

const char *settings_radio_items[] =
{
    "Offset",
    "Direction",
    "Step",
};

const char * settings_m17_items[] =
{
    "Callsign",
    "Meta Txt",
    "CAN",
    "CAN RX Check"
};

const char* settings_fm_items[] =
{
    "Squelch",
    "Width",
    "Power",
    "Tone mode",
    "TX tone",
    "RX tone",
#ifdef CONFIG_ROGER_BEEP
    "Talk permit",
    "Roger beep",
#endif
#ifdef CONFIG_SCAN
    "Scan resume",
#endif
#ifdef CONFIG_VOX
    "VOX",
    "VOX delay",
#endif
#ifdef CONFIG_NOISE_REDUCTION
    "AI denoise RX",
    "AI denoise TX",
#endif
#ifdef CONFIG_DTMF
    "# in TX",
#endif
};

#ifdef CONFIG_TETRA
const char *settings_tetra_items[] =
{
    "Follow calls",
    "Late join",
    "Cell tracking",
    "Alarm beep",
    "Keep filter",
#ifdef CONFIG_TETRA_CALLREC
    "Save calls",
#endif
#ifdef CONFIG_TETRA_AFC
    "Freq. correct",
#endif
};
#endif

const char * settings_accessibility_items[] =
{
    "Macro Latch",
    "Voice",
    "Phonetic"
};

const char *backup_restore_items[] =
{
    "Backup",
    "Restore"
};

const char *info_items[] =
{
    "",
    "Bat. Voltage",
    "Bat. Charge",
    "RSSI",
    "Used heap",
    "Band",
    "VHF",
    "UHF",
    "Hw Version",
#ifdef PLATFORM_TTWRPLUS
    "Radio",
    "Radio FW",
#endif
};

const char *authors[] =
{
#ifdef CONFIG_TETRA
    "C62 fork:",
    "Jackson Zhou",
    "jkzhou.ca",
    "OpenRTX by:",
#endif
    "Niccolo' IU2KIN",
    "Silvano IU2KWO",
    "Federico IU2NUO",
    "Fred IU2NRO",
    "Joseph VK7JS",
    "Morgan ON4MOD",
    "Marco DM4RCO"
};

static const char *symbols_ITU_T_E161[] =
{
    " 0",
    ",.?1",
    "abc2ABC",
    "def3DEF",
    "ghi4GHI",
    "jkl5JKL",
    "mno6MNO",
    "pqrs7PQRS",
    "tuv8TUV",
    "wxyz9WXYZ",
    "-/*",
    "#"
};

static const char *symbols_ITU_T_E161_callsign[] =
{
    "0 ",
    "1",
    "ABC2",
    "DEF3",
    "GHI4",
    "JKL5",
    "MNO6",
    "PQRS7",
    "TUV8",
    "WXYZ9",
    "-/",
    ""
};

// Calculate number of menu entries
const uint8_t menu_num = sizeof(menu_items)/sizeof(menu_items[0]);
const uint8_t settings_num = sizeof(settings_items)/sizeof(settings_items[0]);
const uint8_t display_num = sizeof(display_items)/sizeof(display_items[0]);
#ifdef CONFIG_GPS
const uint8_t settings_gps_num = sizeof(settings_gps_items)/sizeof(settings_gps_items[0]);
#endif
const uint8_t settings_radio_num = sizeof(settings_radio_items)/sizeof(settings_radio_items[0]);
#ifdef CONFIG_M17
const uint8_t settings_m17_num = sizeof(settings_m17_items)/sizeof(settings_m17_items[0]);
#endif
const uint8_t settings_fm_num = sizeof(settings_fm_items) / sizeof(settings_fm_items[0]);
#ifdef CONFIG_MEMCH
const char *settings_memory_items[] =
{
    "Slot",
    "Save here",
    "Rename",
    "Delete"
};
const uint8_t settings_memory_num = sizeof(settings_memory_items) / sizeof(settings_memory_items[0]);
#endif
const uint8_t settings_accessibility_num = sizeof(settings_accessibility_items)/sizeof(settings_accessibility_items[0]);
#ifdef CONFIG_TETRA
const uint8_t settings_tetra_num = sizeof(settings_tetra_items)/sizeof(settings_tetra_items[0]);
#endif
const uint8_t backup_restore_num = sizeof(backup_restore_items)/sizeof(backup_restore_items[0]);
const uint8_t info_num = sizeof(info_items)/sizeof(info_items[0]);
const uint8_t author_num = sizeof(authors)/sizeof(authors[0]);

const color_t color_black = {0, 0, 0, 255};
const color_t color_grey = {60, 60, 60, 255};
const color_t color_white = {255, 255, 255, 255};
const color_t yellow_fab413 = {250, 180, 19, 255};

layout_t layout;
state_t last_state;
bool macro_latched;
static ui_state_t ui_state;
static bool macro_menu = false;
static bool layout_ready = false;
static bool redraw_needed = true;

static bool standby = false;
static long long last_event_tick = 0;

// UI event queue
static uint8_t evQueue_rdPos;
static uint8_t evQueue_wrPos;
static event_t evQueue[MAX_NUM_EVENTS];


static void _ui_calculateLayout(layout_t *layout)
{
    // Horizontal line height
    static const uint16_t hline_h = 1;
    // Compensate for fonts printing below the start position
    static const uint16_t text_v_offset = 1;

    // Calculate UI layout depending on vertical resolution
    // Tytera MD380, MD-UV380
    #if CONFIG_SCREEN_HEIGHT > 127

    // Height and padding shown in diagram at beginning of file
    static const uint16_t top_h = 16;
    static const uint16_t top_pad = 4;
    static const uint16_t line1_h = 20;
    static const uint16_t line2_h = 20;
    static const uint16_t line3_h = 20;
    static const uint16_t line3_large_h = 40;
    static const uint16_t line4_h = 20;
    static const uint16_t menu_h = 16;
    static const uint16_t bottom_h = 23;
    static const uint16_t bottom_pad = top_pad;
    static const uint16_t status_v_pad = 2;
    static const uint16_t small_line_v_pad = 2;
    static const uint16_t big_line_v_pad = 6;
    static const uint16_t horizontal_pad = 4;

    // Top bar font: 8 pt
    static const fontSize_t   top_font = FONT_SIZE_8PT;
    static const symbolSize_t top_symbol_size = SYMBOLS_SIZE_8PT;
    // Text line font: 8 pt
    static const fontSize_t line1_font = FONT_SIZE_8PT;
    static const symbolSize_t line1_symbol_size = SYMBOLS_SIZE_8PT;
    static const fontSize_t line2_font = FONT_SIZE_8PT;
    static const symbolSize_t line2_symbol_size = SYMBOLS_SIZE_8PT;
    static const fontSize_t line3_font = FONT_SIZE_8PT;
    static const symbolSize_t line3_symbol_size = SYMBOLS_SIZE_8PT;
    static const fontSize_t line4_font = FONT_SIZE_8PT;
    static const symbolSize_t line4_symbol_size = SYMBOLS_SIZE_8PT;
    // Message font
    const fontSize_t message_font = FONT_SIZE_6PT;
    // Frequency line font: 16 pt
    static const fontSize_t line3_large_font = FONT_SIZE_16PT;
    // Bottom bar font: 8 pt
    static const fontSize_t bottom_font = FONT_SIZE_8PT;
    // TimeDate/Frequency input font
    static const fontSize_t input_font = FONT_SIZE_12PT;
    // Menu font
    static const fontSize_t menu_font = FONT_SIZE_8PT;

    // Radioddity GD-77
    #elif CONFIG_SCREEN_HEIGHT > 63

    // Height and padding shown in diagram at beginning of file
    static const uint16_t top_h = 11;
    static const uint16_t top_pad = 1;
    static const uint16_t line1_h = 10;
    static const uint16_t line2_h = 10;
    static const uint16_t line3_h = 10;
    static const uint16_t line3_large_h = 16;
    static const uint16_t line4_h = 10;
    static const uint16_t menu_h = 10;
    static const uint16_t bottom_h = 15;
    static const uint16_t bottom_pad = 0;
    static const uint16_t status_v_pad = 1;
    static const uint16_t small_line_v_pad = 1;
    static const uint16_t big_line_v_pad = 0;
    static const uint16_t horizontal_pad = 4;

    // Top bar font: 6 pt
    static const fontSize_t   top_font = FONT_SIZE_6PT;
    static const symbolSize_t top_symbol_size = SYMBOLS_SIZE_6PT;
    // Middle line fonts: 5, 8, 8 pt
    static const fontSize_t line1_font = FONT_SIZE_6PT;
    static const symbolSize_t line1_symbol_size = SYMBOLS_SIZE_6PT;
    static const fontSize_t line2_font = FONT_SIZE_6PT;
    static const symbolSize_t line2_symbol_size = SYMBOLS_SIZE_6PT;
    static const fontSize_t line3_font = FONT_SIZE_6PT;
    static const symbolSize_t line3_symbol_size = SYMBOLS_SIZE_6PT;
    static const fontSize_t line3_large_font = FONT_SIZE_10PT;
    static const fontSize_t line4_font = FONT_SIZE_6PT;
    static const symbolSize_t line4_symbol_size = SYMBOLS_SIZE_6PT;
    // Message font
    const fontSize_t message_font = FONT_SIZE_6PT;
    // Bottom bar font: 6 pt
    static const fontSize_t bottom_font = FONT_SIZE_6PT;
    // TimeDate/Frequency input font
    static const fontSize_t input_font = FONT_SIZE_8PT;
    // Menu font
    static const fontSize_t menu_font = FONT_SIZE_6PT;

    // Radioddity RD-5R
    #elif CONFIG_SCREEN_HEIGHT > 47

    // Height and padding shown in diagram at beginning of file
    static const uint16_t top_h = 11;
    static const uint16_t top_pad = 1;
    static const uint16_t line1_h = 0;
    static const uint16_t line2_h = 10;
    static const uint16_t line3_h = 10;
    static const uint16_t line3_large_h = 18;
    static const uint16_t line4_h = 10;
    static const uint16_t menu_h = 10;
    static const uint16_t bottom_h = 0;
    static const uint16_t bottom_pad = 0;
    static const uint16_t status_v_pad = 1;
    static const uint16_t small_line_v_pad = 1;
    static const uint16_t big_line_v_pad = 0;
    static const uint16_t horizontal_pad = 4;

    // Top bar font: 6 pt
    static const fontSize_t   top_font = FONT_SIZE_6PT;
    static const symbolSize_t top_symbol_size = SYMBOLS_SIZE_6PT;
    // Middle line fonts: 16, 16
    static const fontSize_t line2_font = FONT_SIZE_6PT;
    static const fontSize_t line3_font = FONT_SIZE_6PT;
    static const fontSize_t line4_font = FONT_SIZE_6PT;
    static const fontSize_t line3_large_font = FONT_SIZE_12PT;
    // TimeDate/Frequency input font
    static const fontSize_t input_font = FONT_SIZE_8PT;
    // Menu font
    static const fontSize_t menu_font = FONT_SIZE_6PT;
    // Message font
    const fontSize_t message_font = FONT_SIZE_6PT;
    // Not present on this resolution
    static const fontSize_t line1_font = 0;
    static const fontSize_t bottom_font = 0;

    #else
    #error Unsupported vertical resolution!
    #endif

    // Calculate printing positions
    static const uint16_t top_pos   = top_h - status_v_pad - text_v_offset;
    static const uint16_t line1_pos = top_h + top_pad + line1_h - small_line_v_pad - text_v_offset;
    static const uint16_t line2_pos = top_h + top_pad + line1_h + line2_h - small_line_v_pad - text_v_offset;
    static const uint16_t line3_pos = top_h + top_pad + line1_h + line2_h + line3_h - small_line_v_pad - text_v_offset;
    static const uint16_t line4_pos = top_h + top_pad + line1_h + line2_h + line3_h + line4_h - small_line_v_pad - text_v_offset;
    static const uint16_t line3_large_pos = top_h + top_pad + line1_h + line2_h + line3_large_h - big_line_v_pad - text_v_offset;
    static const uint16_t bottom_pos = CONFIG_SCREEN_HEIGHT - bottom_pad - status_v_pad - text_v_offset;

    layout_t new_layout =
    {
        hline_h,
        top_h,
        line1_h,
        line2_h,
        line3_h,
        line3_large_h,
        line4_h,
        menu_h,
        bottom_h,
        bottom_pad,
        status_v_pad,
        horizontal_pad,
        text_v_offset,
        {horizontal_pad, top_pos},
        {horizontal_pad, line1_pos},
        {horizontal_pad, line2_pos},
        {horizontal_pad, line3_pos},
        {horizontal_pad, line3_large_pos},
        {horizontal_pad, line4_pos},
        {horizontal_pad, bottom_pos},
        top_font,
        top_symbol_size,
        line1_font,
        line1_symbol_size,
        line2_font,
        line2_symbol_size,
        line3_font,
        line3_symbol_size,
        line3_large_font,
        line4_font,
        line4_symbol_size,
        bottom_font,
        input_font,
        menu_font,
        message_font
    };

    memcpy(layout, &new_layout, sizeof(layout_t));
}

static void _ui_drawLowBatteryScreen()
{
    gfx_clearScreen();
    uint16_t bat_width = CONFIG_SCREEN_WIDTH / 2;
    uint16_t bat_height = CONFIG_SCREEN_HEIGHT / 3;
    point_t bat_pos = {CONFIG_SCREEN_WIDTH / 4, CONFIG_SCREEN_HEIGHT / 8};
    gfx_drawBattery(bat_pos, bat_width, bat_height, 10);
    point_t text_pos_1 = {0, CONFIG_SCREEN_HEIGHT * 2 / 3};
    point_t text_pos_2 = {0, CONFIG_SCREEN_HEIGHT * 2 / 3 + 16};

    gfx_print(text_pos_1,
              FONT_SIZE_6PT,
              TEXT_ALIGN_CENTER,
              color_white,
              currentLanguage->forEmergencyUse);
    gfx_print(text_pos_2,
              FONT_SIZE_6PT,
              TEXT_ALIGN_CENTER,
              color_white,
              currentLanguage->pressAnyButton);
}

static freq_t _ui_freq_add_digit(freq_t freq, uint8_t pos, uint8_t number)
{
    freq_t coefficient = 100;
    for(uint8_t i=0; i < FREQ_DIGITS - pos; i++)
    {
        coefficient *= 10;
    }
    return freq += number * coefficient;
}

#ifdef CONFIG_RTC
static void _ui_timedate_add_digit(datetime_t *timedate, uint8_t pos,
                                   uint8_t number)
{
    vp_flush();
    vp_queueInteger(number);
    if (pos == 2 || pos == 4)
        vp_queuePrompt(PROMPT_SLASH);
    // just indicates separation of date and time.
    if (pos==6) // start of time.
        vp_queueString("hh:mm", vpAnnounceCommonSymbols|vpAnnounceLessCommonSymbols);
    if (pos == 8)
        vp_queuePrompt(PROMPT_COLON);
    vp_play();

    switch(pos)
    {
        // Set date
        case 1:
            timedate->date += number * 10;
            break;
        case 2:
            timedate->date += number;
            break;
        // Set month
        case 3:
            timedate->month += number * 10;
            break;
        case 4:
            timedate->month += number;
            break;
        // Set year
        case 5:
            timedate->year += number * 10;
            break;
        case 6:
            timedate->year += number;
            break;
        // Set hour
        case 7:
            timedate->hour += number * 10;
            break;
        case 8:
            timedate->hour += number;
            break;
        // Set minute
        case 9:
            timedate->minute += number * 10;
            break;
        case 10:
            timedate->minute += number;
            break;
    }
}
#endif

static bool _ui_freq_check_limits(freq_t freq)
{
    bool valid = false;
    const hwInfo_t* hwinfo = platform_getHwInfo();
    if(hwinfo->vhf_band)
    {
        // hwInfo_t frequencies are in MHz
        if(freq >= (hwinfo->vhf_minFreq * 1000000) &&
           freq <= (hwinfo->vhf_maxFreq * 1000000))
        valid = true;
    }
    if(hwinfo->uhf_band)
    {
        // hwInfo_t frequencies are in MHz
        if(freq >= (hwinfo->uhf_minFreq * 1000000) &&
           freq <= (hwinfo->uhf_maxFreq * 1000000))
        valid = true;
    }
    return valid;
}

static bool _ui_channel_valid(channel_t* channel)
{
return _ui_freq_check_limits(channel->rx_frequency) &&
       _ui_freq_check_limits(channel->tx_frequency);
}

static bool _ui_drawDarkOverlay()
{
    color_t alpha_grey = {0, 0, 0, 255};
    point_t origin = {0, 0};
    gfx_drawRect(origin, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT, alpha_grey, true);
    return true;
}

static int _ui_fsm_loadChannel(int16_t channel_index, bool *sync_rtx)
{
    channel_t channel;
    int32_t selected_channel = channel_index;
    // If a bank is active, get index from current bank
    if(state.bank_enabled)
    {
        bankHdr_t bank = { 0 };
        cps_readBankHeader(&bank, state.bank);
        if((channel_index < 0) || (channel_index >= bank.ch_count))
            return -1;
        channel_index = cps_readBankData(state.bank, channel_index);
    }

    int result = cps_readChannel(&channel, channel_index);
    // Read successful and channel is valid
    if((result != -1) && _ui_channel_valid(&channel))
    {
        // Set new channel index
        state.channel_index = selected_channel;
        // Copy channel read to state
        state.channel = channel;
        *sync_rtx = true;
    }

    return result;
}

static void _ui_fsm_confirmVFOInput(bool *sync_rtx)
{
    vp_flush();
    // Switch to TX input
    if(ui_state.input_set == SET_RX)
    {
        ui_state.input_set = SET_TX;
        // Reset input position
        ui_state.input_position = 0;
        // announce the rx frequency just confirmed with Enter.
        vp_queueFrequency(ui_state.new_rx_frequency);
        // defer playing till the end.
        // indicate that the user has moved to the tx freq field.
        vp_announceInputReceiveOrTransmit(true, vpqDefault);
    }
    else if(ui_state.input_set == SET_TX)
    {
        // Save new frequency setting
        // If TX frequency was not set, TX = RX
        if(ui_state.new_tx_frequency == 0)
        {
            ui_state.new_tx_frequency = ui_state.new_rx_frequency;
        }
        // Apply new frequencies if they are valid
        if(_ui_freq_check_limits(ui_state.new_rx_frequency) &&
           _ui_freq_check_limits(ui_state.new_tx_frequency))
        {
            state.channel.rx_frequency = ui_state.new_rx_frequency;
            state.channel.tx_frequency = ui_state.new_tx_frequency;
            *sync_rtx = true;
            // force init to clear any prompts in progress.
            // defer play because play is called at the end of the function
            //due to above freq queuing.
            vp_announceFrequencies(state.channel.rx_frequency,
                                   state.channel.tx_frequency, vpqInit);
        }
        else
        {
            vp_announceError(vpqInit);
        }

        state.ui_screen = MAIN_VFO;
    }

    vp_play();
}

#ifdef CONFIG_TETRA
/* In ui_main.c */
extern void _ui_tetraToggleLine1(void);
extern void _ui_drawTetraRecent(ui_state_t *ui_state);
extern void _ui_drawTetraFieldTest(ui_state_t *ui_state);
extern void _ui_drawSettingsTetra(ui_state_t *ui_state);
extern uint32_t _ui_tetraHoldCandidate(void);
extern uint32_t _ui_tetraRecentSsi(uint8_t index);
extern void _ui_drawTetraRecentDetail(ui_state_t *ui_state);
extern void _ui_drawTetraMessages(ui_state_t *ui_state);
extern void _ui_drawTetraMessage(ui_state_t *ui_state);
extern void _ui_drawTetraActivity(ui_state_t *ui_state);
extern void _ui_drawTetraActivityDetail(ui_state_t *ui_state);
extern bool _ui_tetraCallActive(void);

/**
 * Hand the TETRA settings (Settings > TETRA) to the receiver.
 */
static void _ui_tetraApplySettings(void)
{
    tetraConfig_set(!state.settings.tetraFollowOff,
                    !state.settings.tetraJoinOff,
                    !state.settings.tetraRoamOff,
                    !state.settings.tetraAlarmOff);
#ifdef CONFIG_TETRA_CALLREC
    tetraCallRec_enable(state.settings.tetraSaveCalls);
#endif
}

/**
 * Remember the talkgroup filter for the next power on. A held talkgroup
 * is not kept: the filter before it is.
 */
static void _ui_tetraRememberFilter(void)
{
    size_t filter = tetraIds_selectedFilter();

    if (filter < tetraIds_filterCount())
        state.settings.tetraFilter = (uint8_t)filter;
}

/**
 * Change a TETRA setting.
 *
 * @param item: enum settingsTetraItems.
 * @param on: new value.
 */
static void _ui_changeTetraSetting(uint8_t item, bool on)
{
    switch (item) {
        case T_FOLLOW:
            state.settings.tetraFollowOff = !on;
            break;
        case T_JOIN:
            state.settings.tetraJoinOff = !on;
            break;
        case T_ROAM:
            state.settings.tetraRoamOff = !on;
            break;
        case T_ALARM_BEEP:
            state.settings.tetraAlarmOff = !on;
            break;
        case T_KEEP_FILTER:
            state.settings.tetraFilterReset = !on;
            break;
#ifdef CONFIG_TETRA_CALLREC
        case T_SAVE_CALLS:
            // Off, All, Marked, Filter: up the next, down the one before
            if (on && state.settings.tetraSaveCalls + 1 < CALLREC_MODES)
                state.settings.tetraSaveCalls++;
            else if (!on && state.settings.tetraSaveCalls > 0)
                state.settings.tetraSaveCalls--;
            break;
#endif
#ifdef CONFIG_TETRA_AFC
        case T_AFC:
            state.settings.tetraAfcOff = !on;
            break;
#endif
        default:
            return;
    }
    _ui_tetraApplySettings();
}

/*
 * #, * or 0 pressed: its short action waits for the key to be released, so
 * that holding it for the long action does not do the short one first.
 */
static keyboard_t tetraPendingKey;

/**
 * Short press of # or *: next / previous talkgroup filter, or release a
 * held talkgroup. Of 0: recent calls.
 */
static void _ui_tetraShortKey(keyboard_t key)
{
    if(key == KEY_0)
    {
        ui_state.last_main_state = state.ui_screen;
        ui_state.menu_selected = 0;
        state.ui_screen = TETRA_RECENT;
    }
    else if(tetraIds_held() != 0)
        tetraIds_release();
    else if(tetraIds_filterCount() > 1)
        tetraIds_stepFilter((key == KEY_HASH) ? 1 : -1);

    _ui_tetraRememberFilter();
}

/**
 * TETRA keys on the main screens:
 * - # / *, on release: next / previous talkgroup filter, or release a held
 *   talkgroup;
 * - long #: hold the current or last heard talkgroup, or release it;
 * - long *: switch line 1 between the last call heard and the signal;
 * - 0, on release: recent calls;
 * - long 0: search the known carriers for the best signal.
 *
 * @return true if the key was used.
 */
static bool _ui_tetraKeys(kbd_msg_t msg)
{
    if(state.channel.mode != OPMODE_TETRA)
    {
        tetraPendingKey = 0;
        return false;
    }

    // # or * released before its long press: do the short action
    if((tetraPendingKey != 0) && ((msg.keys & tetraPendingKey) == 0))
    {
        _ui_tetraShortKey(tetraPendingKey);
        tetraPendingKey = 0;
        if(msg.keys == 0)
            return true;
    }

    if(msg.long_press)
    {
        if(msg.keys & KEY_HASH)
        {
            tetraPendingKey = 0;
            if(tetraIds_held() != 0)
                tetraIds_release();
            else
                tetraIds_hold(_ui_tetraHoldCandidate());
            return true;
        }
        if(msg.keys & KEY_STAR)
        {
            tetraPendingKey = 0;
            _ui_tetraToggleLine1();
            return true;
        }
        if(msg.keys & KEY_0)
        {
            tetraPendingKey = 0;
            tetraStatus_requestSearch();
            return true;
        }
        return false;
    }

    if(msg.keys & KEY_HASH)
        tetraPendingKey = KEY_HASH;
    else if(msg.keys & KEY_STAR)
        tetraPendingKey = KEY_STAR;
    else if(msg.keys & KEY_0)
        tetraPendingKey = KEY_0;
    else
        return false;

    return true;
}
#endif

int _ui_handleToneSelectScroll(bool direction_up);

#ifdef CONFIG_SELFTEST
static void _ui_menuBack(uint8_t prev_state);

/** Open the self test on the parts page, running the checks */
static void _ui_selfTestOpen(void)
{
    ui_state.selftest_page = SELFTEST_PAGE_PARTS;
    ui_state.selftest_scroll = 0;
    ui_state.selftest_keys_seen = 0;
    ui_state.selftest_keys_last = 0;
    ui_state.selftest_keys_long = false;
    ui_state.selftest_count = (uint8_t)selftest_run(ui_state.selftest_items,
                                                    SELFTEST_ITEMS);
    state.ui_screen = SELF_TEST;
}

/** Go to a page of the self test */
static void _ui_selfTestPage(uint8_t page)
{
    // The microphone level is measured only while it is captured
    audio_setMicMonitor(page == SELFTEST_PAGE_LIVE);
    ui_state.selftest_page = page;
    ui_state.selftest_scroll = 0;
}

/**
 * Keys of the self test. Long red leaves, long * and # change page from
 * any page. On the keys page every other key is only shown. On the others
 * short red leaves too, * and # change page, up and down scroll, and green
 * runs the checks again.
 */
static void _ui_selfTestKey(kbd_msg_t msg)
{
    uint8_t page = ui_state.selftest_page;

    if(msg.keys == 0)
        return;

    if(page == SELFTEST_PAGE_KEYS)
    {
        ui_state.selftest_keys_seen |= msg.keys;
        ui_state.selftest_keys_last = msg.keys;
        ui_state.selftest_keys_long = msg.long_press;
    }

    bool keysPage = page == SELFTEST_PAGE_KEYS;
    bool pageKey = msg.long_press || !keysPage;

    if((msg.keys & KEY_ESC) && pageKey)
    {
        _ui_selfTestPage(SELFTEST_PAGE_PARTS);
        audio_setMicMonitor(false);
        _ui_menuBack(MENU_TOP);
    }
    else if((msg.keys & KEY_HASH) && pageKey)
        _ui_selfTestPage((page + 1) % SELFTEST_PAGES);
    else if((msg.keys & KEY_STAR) && pageKey)
        _ui_selfTestPage((page + SELFTEST_PAGES - 1) % SELFTEST_PAGES);
    else if(keysPage || msg.long_press)
        return;
    else if(msg.keys & (KEY_UP | KNOB_LEFT))
    {
        if(ui_state.selftest_scroll > 0)
            ui_state.selftest_scroll--;
    }
    else if(msg.keys & (KEY_DOWN | KNOB_RIGHT))
        ui_state.selftest_scroll++;
    else if((msg.keys & KEY_ENTER) && page == SELFTEST_PAGE_PARTS)
        ui_state.selftest_count =
            (uint8_t)selftest_run(ui_state.selftest_items, SELFTEST_ITEMS);
}
#endif

/* Tone picker: digits typed to jump are forgotten after this long */
#define TONE_PICK_DIGITS_MS 2000

/**
 * Green on TX tone or RX tone in Settings > FM: the kinds of tone, on the
 * one in use.
 */
static void _ui_tonePickOpen(bool tx)
{
    const channel_t *ch = &state.channel;

    ui_state.tone_pick_tx = tx;
    ui_state.tone_pick_pos = tx ? _ui_tonePickPos(ch->fm.txToneEn,
                                                  ch->fm.txTone, ch->txDcs,
                                                  ch->txDcsInv)
                                : _ui_tonePickPos(ch->fm.rxToneEn,
                                                  ch->fm.rxTone, ch->rxDcs,
                                                  ch->rxDcsInv);
    ui_state.tone_pick_kind = _ui_tonePickKind(ui_state.tone_pick_pos);
    ui_state.tone_pick_list = false;
    ui_state.tone_pick_digits[0] = '\0';
    state.ui_screen = TONE_PICK;
}

/** The choice selected becomes the channel's TX or RX tone */
static void _ui_tonePickApply(void)
{
    bool en, dcs, inv;
    uint8_t idx;

    _ui_tonePickGet(ui_state.tone_pick_pos, &en, &idx, &dcs, &inv);
    if(ui_state.tone_pick_tx)
    {
        state.channel.fm.txToneEn = en;
        if(en)
        {
            state.channel.fm.txTone = idx;
            state.channel.txDcs = dcs;
            state.channel.txDcsInv = inv;
        }
    }
    else
    {
        state.channel.fm.rxToneEn = en;
        if(en)
        {
            state.channel.fm.rxTone = idx;
            state.channel.rxDcs = dcs;
            state.channel.rxDcsInv = inv;
        }
    }
}

/** Open the list of a kind, on the choice in it if it has it */
static void _ui_tonePickList(uint8_t kind)
{
    ui_state.tone_pick_kind = kind;
    ui_state.tone_pick_list = true;
    ui_state.tone_pick_digits[0] = '\0';
    if(_ui_tonePickKind(ui_state.tone_pick_pos) != kind)
        ui_state.tone_pick_pos = _ui_tonePickStart(kind);
}

/**
 * Keys of the tone picker. On the kinds: up and down, green opens the kind's
 * list (Off is used at once), 1 to 3 open CTCSS, DCS and DCS inverted
 * straight away. In a list: up and down move one, * and # to the list of the
 * kind before or after, numbers jump to the tone or code starting with them
 * (885 for 88.5, 023 for D023), green picks, red back to the kinds.
 */
static void _ui_tonePickKey(kbd_msg_t msg, bool *sync_rtx)
{
    uint8_t kinds = _ui_tonePickKinds();
    uint8_t kind = ui_state.tone_pick_kind;
    long long now = getTick();

    if((msg.keys == 0) || msg.long_press)
        return;

    if(!ui_state.tone_pick_list)
    {
        if(input_isNumberPressed(msg))
        {
            uint8_t k = input_getPressedNumber(msg);
            if((k > TP_KIND_OFF) && (k < kinds))
                _ui_tonePickList(k);
        }
        else if(msg.keys & (KEY_UP | KNOB_LEFT))
            ui_state.tone_pick_kind = (kind + kinds - 1) % kinds;
        else if(msg.keys & (KEY_DOWN | KNOB_RIGHT))
            ui_state.tone_pick_kind = (kind + 1) % kinds;
        else if(msg.keys & KEY_ENTER)
        {
            if(kind != TP_KIND_OFF)
            {
                _ui_tonePickList(kind);
                return;
            }
            ui_state.tone_pick_pos = 0;
            _ui_tonePickApply();
            *sync_rtx = true;
            state.ui_screen = SETTINGS_FM;
        }
        else if(msg.keys & KEY_ESC)
            state.ui_screen = SETTINGS_FM;
        return;
    }

    uint16_t start = _ui_tonePickStart(kind);
    uint16_t count = _ui_tonePickEnd(kind) - start;
    uint16_t i = ui_state.tone_pick_pos - start;

    if(input_isNumberPressed(msg))
    {
        size_t n = strlen(ui_state.tone_pick_digits);
        if((now - ui_state.tone_pick_tick > TONE_PICK_DIGITS_MS)
           || (n + 1 >= sizeof(ui_state.tone_pick_digits)))
            n = 0;
        ui_state.tone_pick_digits[n] = '0' + input_getPressedNumber(msg);
        ui_state.tone_pick_digits[n + 1] = '\0';
        ui_state.tone_pick_tick = now;
        int found = _ui_tonePickFind(kind, ui_state.tone_pick_digits);
        if(found >= 0)
            ui_state.tone_pick_pos = (uint16_t)found;
        return;
    }

    ui_state.tone_pick_digits[0] = '\0';

    if(msg.keys & (KEY_UP | KNOB_LEFT))
        ui_state.tone_pick_pos = start + (i + count - 1) % count;
    else if(msg.keys & (KEY_DOWN | KNOB_RIGHT))
        ui_state.tone_pick_pos = start + (i + 1) % count;
    else if(msg.keys & (KEY_STAR | KEY_HASH))
    {
        // The kind before or after, not Off
        uint8_t lists = kinds - 1;
        uint8_t k = kind - 1;
        k = (msg.keys & KEY_STAR) ? (k + lists - 1) % lists
                                  : (k + 1) % lists;
        _ui_tonePickList(k + 1);
    }
    else if(msg.keys & KEY_ENTER)
    {
        _ui_tonePickApply();
        *sync_rtx = true;
        state.ui_screen = SETTINGS_FM;
    }
    else if(msg.keys & KEY_ESC)
    {
        ui_state.tone_pick_list = false;
        ui_state.tone_pick_digits[0] = '\0';
    }
}

/**
 * Change an FM setting (Settings > FM). Width, power and tones are the
 * current channel's, as with the side key shortcuts.
 *
 * @param item: enum settingsFMItems.
 * @param step: +1 or -1.
 */
static void _ui_changeFmSetting(uint8_t item, int step)
{
    static const uint32_t powers[] = { 1000, 2500, 5000 };
    enum vpQueueFlags queueFlags = vp_getVoiceLevelQueueFlags();
    size_t i;

    switch(item)
    {
        case FM_SQUELCH:
            if((step < 0) && (state.settings.sqlLevel > 0))
                state.settings.sqlLevel--;
            else if((step > 0) && (state.settings.sqlLevel < 15))
                state.settings.sqlLevel++;
            vp_announceSquelch(state.settings.sqlLevel, queueFlags);
            break;

        case FM_WIDTH:
            state.channel.bandwidth = (state.channel.bandwidth == BW_12_5)
                                    ? BW_25 : BW_12_5;
            vp_announceBandwidth(state.channel.bandwidth, queueFlags);
            break;

        case FM_POWER:
            // Nearest level, then the next one up or down
            for(i = 0; i < 2 && state.channel.power > powers[i]; i++)
                ;
            if((step > 0) && (i < 2))
                i++;
            else if((step < 0) && (i > 0))
                i--;
            state.channel.power = powers[i];
            vp_announcePower(state.channel.power, queueFlags);
            break;

        case FM_TONE_MODE:
            _ui_handleToneSelectScroll(step > 0);
            vp_announceCTCSS(state.channel.fm.rxToneEn,
                             state.channel.fm.rxTone,
                             state.channel.fm.txToneEn,
                             state.channel.fm.txTone,
                             queueFlags | vpqIncludeDescriptions);
            break;

        case FM_TX_TONE: {
            uint8_t idx = state.channel.fm.txTone;
            bool dcs = state.channel.txDcs, inv = state.channel.txDcsInv;
            _ui_stepTone(&idx, &dcs, &inv, step);
            state.channel.fm.txTone = idx;
            state.channel.txDcs = dcs;
            state.channel.txDcsInv = inv;
            vp_announceCTCSS(state.channel.fm.rxToneEn,
                             state.channel.fm.rxTone,
                             state.channel.fm.txToneEn,
                             state.channel.fm.txTone, queueFlags);
            break;
        }

#ifdef CONFIG_ROGER_BEEP
        case FM_TALK_PERMIT:
            state.settings.talkPermit =
                (uint8_t)((state.settings.talkPermit + TALK_PERMIT_STYLES + step)
                          % TALK_PERMIT_STYLES);
            break;

        case FM_ROGER_BEEP:
            state.settings.rogerBeep =
                (uint8_t)((state.settings.rogerBeep + ROGER_BEEP_STYLES + step)
                          % ROGER_BEEP_STYLES);
            break;
#endif

#ifdef CONFIG_DTMF
        case FM_TX_HASH:
            state.settings.txHash1750 = !state.settings.txHash1750;
            break;
#endif

#ifdef CONFIG_NOISE_REDUCTION
        case FM_NR_RX:
            state.settings.nrRx =
                (uint8_t)((state.settings.nrRx + 11 + step) % 11);
            break;

        case FM_NR_TX:
            state.settings.nrTx =
                (uint8_t)((state.settings.nrTx + 11 + step) % 11);
            break;
#endif

#ifdef CONFIG_VOX
        case FM_VOX:
            state.settings.voxLevel =
                (uint8_t)((state.settings.voxLevel + VOX_LEVELS + 1 + step)
                          % (VOX_LEVELS + 1));
            break;

        case FM_VOX_DELAY:
            state.settings.voxDelay =
                (uint8_t)((state.settings.voxDelay + VOX_DELAYS + step)
                          % VOX_DELAYS);
            break;
#endif

#ifdef CONFIG_SCAN
        case FM_SCAN_RESUME:
            state.settings.scanResume =
                (uint8_t)((state.settings.scanResume + SCAN_RESUME_NUM + step)
                          % SCAN_RESUME_NUM);
            break;
#endif

        case FM_RX_TONE: {
            uint8_t idx = state.channel.fm.rxTone;
            bool dcs = state.channel.rxDcs, inv = state.channel.rxDcsInv;
            _ui_stepTone(&idx, &dcs, &inv, step);
            state.channel.fm.rxTone = idx;
            state.channel.rxDcs = dcs;
            state.channel.rxDcsInv = inv;
            vp_announceCTCSS(state.channel.fm.rxToneEn,
                             state.channel.fm.rxTone,
                             state.channel.fm.txToneEn,
                             state.channel.fm.txTone, queueFlags);
            break;
        }

        default:
            break;
    }
}

#ifdef CONFIG_FAST_COPY
/* In ui_modes.c */
extern void _ui_drawFastCopy(ui_state_t *ui_state);

/**
 * Long * in FM, on the main screens: fast copy, reading the frequency and
 * tone of a radio transmitting next to this one.
 *
 * @return true if the key was used.
 */
static bool _ui_fastCopyKey(kbd_msg_t msg)
{
    if((state.channel.mode != OPMODE_FM) || !msg.long_press
       || (msg.keys != KEY_STAR))
        return false;

    ui_state.last_main_state = state.ui_screen;
    fastCopy_start();
    state.ui_screen = FAST_COPY;
    return true;
}

/**
 * Copy what fast copy found into the VFO, and go to VFO mode.
 */
static void _ui_fastCopyApply(bool *sync_rtx)
{
    struct fastCopyResult r;
    fastCopy_get(&r);
    if(r.state != FAST_COPY_FOUND)
        return;

    // From memory mode: the VFO, which then takes the copy
    if(ui_state.last_main_state == MAIN_MEM)
        state.channel = state.vfo_channel;

    freq_t hz = fastCopy_channel(r.hz);
    state.channel.mode = OPMODE_FM;
    state.channel.rx_frequency = hz;
    state.channel.tx_frequency = hz;
    state.channel.fm.rxToneEn = r.toneFound;
    state.channel.fm.txToneEn = r.toneFound;
    if(r.toneFound)
    {
        uint8_t idx = 0;
        bool dcs = (r.tone & RTX_TONE_DCS) != 0;
        if(dcs)
        {
            for(uint8_t i = 0; i < DCS_CODE_NUM; i++)
            {
                if(dcs_code[i] == (r.tone & 0x1ff))
                    idx = i;
            }
        }
        else
        {
            // The nearest CTCSS tone
            uint16_t best = 0xffff;
            for(uint8_t i = 0; i < CTCSS_FREQ_NUM; i++)
            {
                uint16_t d = (ctcss_tone[i] > r.tone) ? ctcss_tone[i] - r.tone
                                                      : r.tone - ctcss_tone[i];
                if(d < best)
                {
                    best = d;
                    idx = i;
                }
            }
        }
        bool inv = (r.tone & RTX_TONE_DCS_INV) != 0;
        state.channel.fm.rxTone = idx;
        state.channel.fm.txTone = idx;
        state.channel.rxDcs = dcs;
        state.channel.txDcs = dcs;
        state.channel.rxDcsInv = dcs && inv;
        state.channel.txDcsInv = dcs && inv;
    }
    state.vfo_channel = state.channel;
    state.ui_screen = MAIN_VFO;
    *sync_rtx = true;
}
#endif

#ifdef CONFIG_SCAN
/*
 * FM scan, from a long 0: memory mode goes through the memory channels (the
 * FM ones), VFO mode steps the frequency through its band. It hops on each
 * status update (100 ms) while nothing is heard, and stops where the
 * squelch opens.
 */

/* After a hop, before the signal there counts: the rtx thread retunes and
 * the RSSI filter settles */
#define SCAN_SETTLE_MS 90
/* A carrier on a channel with a receive tone: how long to wait for the
 * tone before going on */
#define SCAN_TONE_MS 600
/* Status updates in a row with the squelch open before stopping */
#define SCAN_OPENS 2
/* Carrier resume: going on once nothing is heard for this long */
#define SCAN_CARRIER_MS 2000
/* Time resume: going on this long after stopping */
#define SCAN_TIME_MS 5000

/**
 * @return the number of memory channels, in the bank if one is in use.
 */
static uint16_t _ui_scanChannelCount(void)
{
    if(state.bank_enabled)
    {
        bankHdr_t bank = { 0 };
        cps_readBankHeader(&bank, state.bank);
        return bank.ch_count;
    }

    channel_t ch;
    uint16_t n = 0;
    while((n < 4096) && (cps_readChannel(&ch, n) != -1))
        n++;
    return n;
}

/** The next memory channel, the FM ones only */
static void _ui_scanHopMemory(bool *sync_rtx)
{
    uint16_t count = ui_state.scan_count;
    int32_t idx = state.channel_index;

    for(uint16_t tries = 0; tries < count; tries++)
    {
        idx = (idx + ui_state.scan_dir + count) % count;
        if((_ui_fsm_loadChannel((int16_t)idx, sync_rtx) != -1)
           && (state.channel_index == idx)
           && (state.channel.mode == OPMODE_FM))
            return;
    }
}

/** The next frequency, one step on, within the band of the current one */
static void _ui_scanHopVfo(bool *sync_rtx)
{
    const hwInfo_t *hw = platform_getHwInfo();
    freq_t rx = state.channel.rx_frequency;
    int32_t shift = (int32_t)(state.channel.tx_frequency - rx);
    freq_t step = freq_steps[state.step_index];
    freq_t lo = hw->uhf_minFreq * 1000000u;
    freq_t hi = hw->uhf_maxFreq * 1000000u;

    if(hw->vhf_band && (rx <= hw->vhf_maxFreq * 1000000u))
    {
        lo = hw->vhf_minFreq * 1000000u;
        hi = hw->vhf_maxFreq * 1000000u;
    }

    if(ui_state.scan_dir > 0)
        rx = (rx + step > hi) ? lo : rx + step;
    else
        rx = (rx < lo + step) ? hi : rx - step;

    state.channel.rx_frequency = rx;
    state.channel.tx_frequency = rx + shift;
    if(!_ui_freq_check_limits(state.channel.tx_frequency))
        state.channel.tx_frequency = rx;
    state.vfo_channel = state.channel;
    *sync_rtx = true;
}

static void _ui_scanHop(long long now, bool *sync_rtx)
{
    if(state.ui_screen == MAIN_MEM)
        _ui_scanHopMemory(sync_rtx);
    else
        _ui_scanHopVfo(sync_rtx);

    ui_state.scan_stopped = false;
    ui_state.scan_opens = 0;
    ui_state.scan_tick = now;
}

static void _ui_scanStop(void)
{
    ui_state.scan_on = false;
    ui_state.scan_stopped = false;
}

/**
 * Long 0 in FM, on the main screens: start scanning upwards.
 */
static void _ui_scanStart(bool *sync_rtx)
{
    if(state.channel.mode != OPMODE_FM)
        return;

    if(state.ui_screen == MAIN_MEM)
    {
        ui_state.scan_count = _ui_scanChannelCount();
        if(ui_state.scan_count < 2)
            return;
    }

    ui_state.scan_on = true;
    ui_state.scan_dir = 1;
    _ui_scanHop(getTick(), sync_rtx);
}

/**
 * Keys while scanning: up and down turn it that way and hop at once, any
 * other key stops it on the channel where it is.
 *
 * @return true if the key was used.
 */
static bool _ui_scanKey(kbd_msg_t msg, bool *sync_rtx)
{
    if(!ui_state.scan_on)
        return false;

    // Releases, and the long press of the key that started it
    if((msg.keys == 0) || msg.long_press)
        return true;

    if(msg.keys & (KEY_UP | KNOB_RIGHT))
    {
        ui_state.scan_dir = 1;
        _ui_scanHop(getTick(), sync_rtx);
    }
    else if(msg.keys & (KEY_DOWN | KNOB_LEFT))
    {
        ui_state.scan_dir = -1;
        _ui_scanHop(getTick(), sync_rtx);
    }
    else
    {
        _ui_scanStop();
    }
    return true;
}

/**
 * On each status update: hop on, stop on a signal, or go on after one.
 */
static void _ui_scanTick(long long now, bool *sync_rtx)
{
    if(!ui_state.scan_on)
        return;

    // Out of the main screens, out of FM or transmitting (PTT or VOX): over
    bool main = (state.ui_screen == MAIN_MEM) || (state.ui_screen == MAIN_VFO);
    bool tx = platform_getPttStatus()
           || (rtx_getCurrentStatus().opStatus == TX);
    if(!main || (state.channel.mode != OPMODE_FM) || tx)
    {
        _ui_scanStop();
        return;
    }

    bool open = rtx_rxSquelchOpen();

    if(ui_state.scan_stopped)
    {
        if(open)
            ui_state.scan_heard = now;

        bool resume = false;
        if(state.settings.scanResume == SCAN_RESUME_TIME)
            resume = (now - ui_state.scan_tick) >= SCAN_TIME_MS;
        else
            resume = (now - ui_state.scan_heard) >= SCAN_CARRIER_MS;
        if(resume)
            _ui_scanHop(now, sync_rtx);
        return;
    }

    if((now - ui_state.scan_tick) < SCAN_SETTLE_MS)
        return;

    if(open)
    {
        if(++ui_state.scan_opens < SCAN_OPENS)
            return;
        if(state.settings.scanResume == SCAN_RESUME_STOP)
        {
            _ui_scanStop();
            return;
        }
        ui_state.scan_stopped = true;
        ui_state.scan_tick = now;
        ui_state.scan_heard = now;
        return;
    }
    ui_state.scan_opens = 0;

    // A carrier, but the tone squelch is still closed: wait a little for
    // the tone, unless it is someone else's
    rssi_t sql = -127 + (state.settings.sqlLevel * 66) / 15;
    if(state.channel.fm.rxToneEn && (rtx_getRssi() > sql + 1)
       && ((now - ui_state.scan_tick) < SCAN_TONE_MS))
        return;

    _ui_scanHop(now, sync_rtx);
}
#endif

#ifdef CONFIG_SPECTRUM
/* In ui_modes.c */
extern void _ui_drawSpectrum(ui_state_t *ui_state);

#define SPECTRUM_MIN_HZ 136000000u
#define SPECTRUM_MAX_HZ 600000000u

static void _ui_spectrumView(void)
{
    uint32_t half = _ui_spectrumStep(ui_state.spec_zoom) * (SPECTRUM_BINS / 2);

    if(ui_state.spec_center < SPECTRUM_MIN_HZ + half)
        ui_state.spec_center = SPECTRUM_MIN_HZ + half;
    if(ui_state.spec_center > SPECTRUM_MAX_HZ - half)
        ui_state.spec_center = SPECTRUM_MAX_HZ - half;
    spectrum_setView(ui_state.spec_center,
                     _ui_spectrumStep(ui_state.spec_zoom));
}

/**
 * Side key + 0 in FM, on the main screens: the spectrum view, around the
 * frequency in use.
 */
static void _ui_spectrumOpen(void)
{
    ui_state.last_main_state = state.ui_screen;
    ui_state.spec_center = state.channel.rx_frequency;
    ui_state.spec_zoom = SPECTRUM_ZOOM_START;
    _ui_spectrumView();
    spectrum_start(ui_state.spec_center,
                   _ui_spectrumStep(ui_state.spec_zoom));
    state.ui_screen = SPECTRUM;
}

/**
 * Green in the spectrum view: the VFO to the strongest signal.
 */
static void _ui_spectrumTune(bool *sync_rtx)
{
    struct spectrumSweep s;
    spectrum_get(&s);
    int peak = _ui_spectrumPeak(&s);
    if(peak < 0)
        return;

    freq_t hz = spectrum_binHz(s.center, s.step, (unsigned)peak);
    if(!_ui_freq_check_limits(hz))
        return;

    if(ui_state.last_main_state == MAIN_MEM)
        state.channel = state.vfo_channel;
    state.channel.mode = OPMODE_FM;
    state.channel.rx_frequency = hz;
    state.channel.tx_frequency = hz;
    state.vfo_channel = state.channel;
    state.ui_screen = MAIN_VFO;
    *sync_rtx = true;
}

/**
 * Keys of the spectrum view: up and down move it a quarter of its width,
 * * and # zoom in and out, green tunes the VFO to the strongest signal,
 * red goes back.
 */
static void _ui_spectrumKey(kbd_msg_t msg, bool *sync_rtx)
{
    uint32_t quarter = _ui_spectrumStep(ui_state.spec_zoom)
                     * (SPECTRUM_BINS / 4);

    if(msg.keys == 0)
        return;

    if(msg.keys & (KEY_UP | KNOB_RIGHT))
    {
        ui_state.spec_center += quarter;
        _ui_spectrumView();
    }
    else if(msg.keys & (KEY_DOWN | KNOB_LEFT))
    {
        ui_state.spec_center -= quarter;
        _ui_spectrumView();
    }
    else if(msg.long_press)
    {
        // Nothing held does more
    }
    else if(msg.keys & KEY_STAR)
    {
        if(ui_state.spec_zoom > 0)
            ui_state.spec_zoom--;
        _ui_spectrumView();
    }
    else if(msg.keys & KEY_HASH)
    {
        if(ui_state.spec_zoom + 1 < SPECTRUM_ZOOMS)
            ui_state.spec_zoom++;
        _ui_spectrumView();
    }
    else if(msg.keys & KEY_ENTER)
    {
        _ui_spectrumTune(sync_rtx);
        if(state.ui_screen != SPECTRUM)
            spectrum_stop();
    }
    else if(msg.keys & KEY_ESC)
    {
        spectrum_stop();
        state.ui_screen = ui_state.last_main_state;
    }
}
#endif

#ifdef CONFIG_TETRA_CALLREC
/* In ui_main.c */
extern void _ui_drawTetraSaved(ui_state_t *ui_state);
static void _ui_menuBack(uint8_t prev_state);

/**
 * Keys of the saved calls: up / down select, green plays (on the one
 * playing: stops), held twice deletes, red stops or goes back. Green acts
 * when let go, so that holding it only arms the delete.
 */
static void _ui_tetraSavedKey(kbd_msg_t msg)
{
    static bool greenHeld;
    uint16_t count = callStore_count();
    uint32_t seq = 0;
    uint16_t played, length;
    bool playing = tetraCallRec_playing(&seq, &played, &length);
    struct callStoreInfo ci;

    if (greenHeld && (msg.keys & KEY_ENTER) == 0) {
        greenHeld = false;
        ui_state.saved_delete_armed = false;
        if (callStore_info(ui_state.saved_sel, &ci)) {
            if (playing && seq == ci.seq)
                tetraCallRec_stop();
            else
                tetraCallRec_play(ci.seq);
        }
        if (msg.keys == 0)
            return;
    }

    if (msg.long_press) {
        if (greenHeld && (msg.keys & KEY_ENTER)) {
            greenHeld = false;
            if (!ui_state.saved_delete_armed) {
                ui_state.saved_delete_armed = true;
            } else if (callStore_info(ui_state.saved_sel, &ci)) {
                if (playing && seq == ci.seq)
                    tetraCallRec_stop();
                callStore_delete(ci.seq);
                ui_state.saved_delete_armed = false;
                count = callStore_count();
                if (ui_state.saved_sel >= count && count > 0)
                    ui_state.saved_sel = count - 1;
            }
        }
        return;
    }

    if (msg.keys & KEY_ENTER) {
        greenHeld = true;
    } else if (msg.keys & (KEY_UP | KNOB_LEFT)) {
        ui_state.saved_delete_armed = false;
        if (ui_state.saved_sel > 0)
            ui_state.saved_sel--;
    } else if (msg.keys & (KEY_DOWN | KNOB_RIGHT)) {
        ui_state.saved_delete_armed = false;
        if (ui_state.saved_sel + 1 < count)
            ui_state.saved_sel++;
    } else if (msg.keys & KEY_ESC) {
        ui_state.saved_delete_armed = false;
        if (playing)
            tetraCallRec_stop();
        else
            _ui_menuBack(MENU_TOP);
    }
}
#endif

#ifdef CONFIG_DTMF
/**
 * Keypad keys while transmitting in FM: their DTMF tones, for as long as
 * they are held. # is 1750 Hz instead with Settings > FM > # in TX.
 *
 * @return true if the key was used.
 */
static bool _ui_dtmfKey(kbd_msg_t msg)
{
    bool tx = (state.channel.mode == OPMODE_FM)
           && (rtx_getCurrentStatus().opStatus == TX);
    bool held = dtmf_current() != 0;

    if (!tx) {
        if (held)
            dtmf_key(0);
        return false;
    }

    char key = 0;
    keyboard_t number = msg.keys & KBD_NUM_MASK;
    if (number != 0 && msg.keys == number && (number & (number - 1)) == 0)
        key = (char)('0' + __builtin_ctz(number));
    else if (msg.keys == KEY_STAR)
        key = '*';
    else if (msg.keys == KEY_HASH && !state.settings.txHash1750)
        key = '#';

    if (key != 0) {
        dtmf_key(key);
        return true;
    }
    if (held && (msg.keys & (KBD_NUM_MASK | KEY_STAR | KEY_HASH)) == 0) {
        dtmf_key(0);
        return msg.keys == 0;
    }
    return false;
}
#endif

#ifdef CONFIG_REPLAY
/* In ui_modes.c */
extern void _ui_drawReplay(void);

/**
 * P2 on the main screens and the FM radio: instant replay of the last
 * transmission received; again while it plays, the one before. Held, it
 * stops. Acts when let go, so that holding it only stops.
 *
 * @return true if the key was used.
 */
static bool _ui_replayKey(kbd_msg_t msg)
{
    static bool pending;

    bool here = (state.ui_screen == MAIN_VFO) || (state.ui_screen == MAIN_MEM);
    #ifdef CONFIG_BCAST_FM
    here = here || (state.ui_screen == BCAST_FM);
    #endif
    if (!here) {
        pending = false;
        return false;
    }

    if (pending && (msg.keys & KEY_F2) == 0) {
        pending = false;
        replay_play();
        return msg.keys == 0;
    }
    if ((msg.keys & KEY_F2) == 0)
        return false;

    if (msg.long_press) {
        pending = false;
        replay_stop();
    } else {
        pending = true;
    }
    return true;
}
#endif

#ifdef CONFIG_BCAST_FM
/* In ui_modes.c */
extern void _ui_drawBcast(ui_state_t *ui_state);

/* A short message on the broadcast FM screen lasts this long */
#define BCAST_NOTE_MS 1500

static void _ui_bcastNote(const char *note)
{
    ui_state.bcast_note = note;
    ui_state.bcast_note_tick = getTick();
}

/** The station listened to, kept in the settings */
static uint32_t _ui_bcastHz(void)
{
    struct bcastStatus st;
    bcast_get(&st);
    if (st.state != BCAST_OFF)
        return st.hz;
    return bcast_clamp((uint32_t)state.settings.bcastFreq * 100000u);
}

static void _ui_bcastTune(uint32_t hz)
{
    hz = bcast_clamp(hz);
    state.settings.bcastFreq = (uint16_t)(hz / 100000u);
    bcast_tune(hz);
}

/**
 * Menu > FM radio: listen to broadcast FM, on the station last listened to.
 */
static void _ui_bcastOpen(bool *sync_rtx)
{
    // PTT still transmits on the channel, as on the main screens
    state.txDisable = false;
    *sync_rtx = true;
    ui_state.bcast_digits[0] = '\0';
    ui_state.bcast_pending = 0;
    ui_state.bcast_note = NULL;
    if (state.channel.mode == OPMODE_FM)
        bcast_start(bcast_clamp((uint32_t)state.settings.bcastFreq * 100000u));
    state.ui_screen = BCAST_FM;
}

static void _ui_bcastClose(void)
{
    if (bcast_requested())
        state.settings.bcastFreq = (uint16_t)(_ui_bcastHz() / 100000u);
    bcast_stop();
    state.ui_screen = ui_state.last_main_state;
}

/**
 * A frequency typed: 3 digits (98.1), or 4 from 100 MHz up (101.5). Green
 * takes fewer as whole MHz (88 for 88.0).
 *
 * @param whole: green was pressed.
 * @return true if the digits were used.
 */
static bool _ui_bcastTyped(bool whole)
{
    const char *d = ui_state.bcast_digits;
    size_t n = strlen(d);
    size_t full = (d[0] == '1') ? 4 : 3;
    uint32_t v = (uint32_t)atoi(d);

    if (n == 0)
        return false;
    if (n < full && !whole)
        return false;

    uint32_t hz = (n == full) ? v * 100000u : v * 1000000u;
    ui_state.bcast_digits[0] = '\0';
    if (hz < BCAST_MIN_HZ || hz > BCAST_MAX_HZ) {
        _ui_bcastNote("not in 87.5-108");
        return true;
    }
    _ui_bcastTune(hz);
    return true;
}

/** The next saved station up (+1) or down (-1) from the one playing */
static void _ui_bcastPreset(int dir)
{
    uint16_t now = (uint16_t)(_ui_bcastHz() / 100000u);
    int idx = -1;

    for (int i = 0; i < BCAST_PRESETS; i++) {
        if (state.settings.bcastPreset[i] == now)
            idx = i;
    }
    for (int k = 1; k <= BCAST_PRESETS; k++) {
        int i = ((idx < 0 ? (dir > 0 ? -1 : 0) : idx) + dir * k
                 + 2 * BCAST_PRESETS) % BCAST_PRESETS;
        if (state.settings.bcastPreset[i] != 0) {
            _ui_bcastTune((uint32_t)state.settings.bcastPreset[i] * 100000u);
            return;
        }
    }
    _ui_bcastNote("no stations saved");
}

/**
 * Keys of the broadcast FM screen: up / down tune 100 kHz, held they seek;
 * numbers type a frequency, held they save the station there; * and #
 * go through the saved stations; red goes back. A number or arrow acts when
 * let go, so that holding it does only its long press.
 */
static void _ui_bcastKey(kbd_msg_t msg)
{
    static const char *saved[BCAST_PRESETS] = {
        "saved on 1", "saved on 2", "saved on 3", "saved on 4", "saved on 5",
        "saved on 6", "saved on 7", "saved on 8", "saved on 9",
    };
    uint32_t pending = ui_state.bcast_pending;
    bool fm = state.channel.mode == OPMODE_FM;

    if (msg.keys & KEY_ESC) {
        if (ui_state.bcast_digits[0] != '\0')
            ui_state.bcast_digits[0] = '\0';
        else
            _ui_bcastClose();
        ui_state.bcast_pending = 0;
        return;
    }
    if (!fm)
        return;

    // Let go before its long press: the short action
    if (pending != 0 && (msg.keys & pending) == 0) {
        ui_state.bcast_pending = 0;
        if (pending & (KEY_UP | KEY_DOWN)) {
            _ui_bcastTune(bcast_step(_ui_bcastHz(),
                                     (pending & KEY_UP) ? 1 : -1));
        } else {
            size_t n = strlen(ui_state.bcast_digits);
            if (n + 1 < sizeof(ui_state.bcast_digits)) {
                ui_state.bcast_digits[n] = '0' + __builtin_ctz(pending);
                ui_state.bcast_digits[n + 1] = '\0';
            }
            _ui_bcastTyped(false);
        }
        if (msg.keys == 0)
            return;
    }

    if (msg.long_press) {
        if (pending & (KEY_UP | KEY_DOWN)) {
            ui_state.bcast_pending = 0;
            ui_state.bcast_digits[0] = '\0';
            bcast_seek((pending & KEY_UP) ? 1 : -1);
        } else if (pending & KBD_NUM_MASK) {
            uint8_t n = (uint8_t)__builtin_ctz(pending);
            ui_state.bcast_pending = 0;
            if (n >= 1 && n <= BCAST_PRESETS) {
                state.settings.bcastPreset[n - 1] =
                    (uint16_t)(_ui_bcastHz() / 100000u);
                _ui_bcastNote(saved[n - 1]);
            }
        }
        return;
    }

    if (msg.keys == KEY_UP || msg.keys == KEY_DOWN) {
        ui_state.bcast_pending = msg.keys;
    } else if (msg.keys & KNOB_RIGHT) {
        _ui_bcastTune(bcast_step(_ui_bcastHz(), 1));
    } else if (msg.keys & KNOB_LEFT) {
        _ui_bcastTune(bcast_step(_ui_bcastHz(), -1));
    } else if (input_isNumberPressed(msg)) {
        keyboard_t number = msg.keys & KBD_NUM_MASK;
        if ((number & (number - 1)) == 0)
            ui_state.bcast_pending = number;
    } else if (msg.keys & KEY_STAR) {
        _ui_bcastPreset(-1);
    } else if (msg.keys & KEY_HASH) {
        _ui_bcastPreset(1);
    } else if (msg.keys & KEY_ENTER) {
        _ui_bcastTyped(true);
    }
}
#endif

#ifdef CONFIG_MEMCH
/* In ui_menu.c */
extern void _ui_drawSettingsMemory(ui_state_t *ui_state);

static void _ui_textInputReset(char *buf, size_t bufSize);
static void _ui_textInputKeypad(char *buf, uint8_t max_len, kbd_msg_t msg,
                                bool callsign);
static void _ui_textInputConfirm(char *buf);
static void _ui_textInputDel(char *buf);
static void _ui_menuUp(uint8_t menu_entries);
static void _ui_menuDown(uint8_t menu_entries);
static void _ui_menuBack(uint8_t prev_state);

/**
 * Settings > Memory, as the Quansheng UV-K5's channel menus: Slot picks
 * M1 to M200 (Enter, then Up / Down); Enter on Save here saves the channel
 * in use (FM, the VFO or a memory channel with its changes) to that slot;
 * on Rename types its name on the keypad; on Delete, twice, clears it.
 */
static void _ui_memoryKeys(kbd_msg_t msg)
{
    uint8_t slot = ui_state.mem_slot;

    // Typing a name
    if(ui_state.mem_naming)
    {
        if(msg.keys & KEY_ENTER)
        {
            _ui_textInputConfirm(ui_state.new_name);
            char *p = ui_state.new_name;
            if(p[0] == '_')
                p[0] = '\0';
            ui_state.mem_status = (memch_rename(slot, p) == 0) ? "renamed"
                                                                 : "write failed";
            ui_state.mem_naming = false;
        }
        else if(msg.keys & KEY_ESC)
        {
            ui_state.mem_naming = false;
        }
        else if(msg.keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))
        {
            _ui_textInputDel(ui_state.new_name);
        }
        else if(input_isCharPressed(msg))
        {
            _ui_textInputKeypad(ui_state.new_name, MEMCH_NAME_LEN, msg, false);
        }
        return;
    }

    // Choosing the slot
    if(ui_state.edit_mode && (ui_state.menu_selected == MEM_SLOT))
    {
        if(msg.keys & (KEY_UP | KNOB_RIGHT | KEY_RIGHT))
            ui_state.mem_slot = (uint8_t)((slot + 1) % MEMCH_SLOTS);
        else if(msg.keys & (KEY_DOWN | KNOB_LEFT | KEY_LEFT))
            ui_state.mem_slot = (uint8_t)((slot + MEMCH_SLOTS - 1) % MEMCH_SLOTS);
        else if(msg.keys & (KEY_ENTER | KEY_ESC))
            ui_state.edit_mode = false;
        ui_state.mem_status = NULL;
        ui_state.mem_delete_armed = false;
        return;
    }

    if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
    {
        _ui_menuUp(settings_memory_num);
        ui_state.mem_delete_armed = false;
    }
    else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
    {
        _ui_menuDown(settings_memory_num);
        ui_state.mem_delete_armed = false;
    }
    else if(msg.keys & KEY_ESC)
    {
        ui_state.mem_delete_armed = false;
        _ui_menuBack(MENU_SETTINGS);
    }
    else if(msg.keys & KEY_ENTER)
    {
        switch(ui_state.menu_selected)
        {
            case MEM_SLOT:
                ui_state.edit_mode = true;
                break;

            case MEM_SAVE:
                if(state.channel.mode != OPMODE_FM)
                    ui_state.mem_status = "FM only";
                else if(memch_save(slot, &state.channel) == 0)
                    ui_state.mem_status = "saved";
                else
                    ui_state.mem_status = "write failed";
                break;

            case MEM_RENAME:
                if(!memch_used(slot))
                {
                    ui_state.mem_status = "empty slot";
                    break;
                }
                _ui_textInputReset(ui_state.new_name, sizeof(ui_state.new_name));
                ui_state.mem_naming = true;
                break;

            case MEM_DELETE:
                if(!memch_used(slot))
                {
                    ui_state.mem_status = "empty slot";
                }
                else if(!ui_state.mem_delete_armed)
                {
                    ui_state.mem_delete_armed = true;
                }
                else
                {
                    ui_state.mem_delete_armed = false;
                    ui_state.mem_status = (memch_delete(slot) == 0) ? "deleted"
                                                                    : "write failed";
                }
                break;
        }
    }
}
#endif

static void _ui_fsm_insertVFONumber(kbd_msg_t msg, bool *sync_rtx)
{
    // Advance input position
    ui_state.input_position += 1;
    // clear any prompts in progress.
    vp_flush();
    // Save pressed number to calculate frequency and show in GUI
    ui_state.input_number = input_getPressedNumber(msg);
    // queue the digit just pressed.
    vp_queueInteger(ui_state.input_number);
    // queue  point if user has entered three digits.
    if (ui_state.input_position == 3)
        vp_queuePrompt(PROMPT_POINT);

    if(ui_state.input_set == SET_RX)
    {
        if(ui_state.input_position == 1)
            ui_state.new_rx_frequency = 0;
        // Calculate portion of the new RX frequency
        ui_state.new_rx_frequency = _ui_freq_add_digit(ui_state.new_rx_frequency,
                                                       ui_state.input_position,
                                                       ui_state.input_number);
        if(ui_state.input_position >= FREQ_DIGITS)
        {// queue the rx freq just completed.
            vp_queueFrequency(ui_state.new_rx_frequency);
            /// now queue tx as user has changed fields.
            vp_queuePrompt(PROMPT_TRANSMIT);
            // Switch to TX input
            ui_state.input_set = SET_TX;
            // Reset input position
            ui_state.input_position = 0;
            // Reset TX frequency
            ui_state.new_tx_frequency = 0;
        }
    }
    else if(ui_state.input_set == SET_TX)
    {
        if(ui_state.input_position == 1)
            ui_state.new_tx_frequency = 0;
        // Calculate portion of the new TX frequency
        ui_state.new_tx_frequency = _ui_freq_add_digit(ui_state.new_tx_frequency,
                                                       ui_state.input_position,
                                                       ui_state.input_number);
        if(ui_state.input_position >= FREQ_DIGITS)
        {
            // Save both inserted frequencies
            if(_ui_freq_check_limits(ui_state.new_rx_frequency) &&
               _ui_freq_check_limits(ui_state.new_tx_frequency))
            {
                state.channel.rx_frequency = ui_state.new_rx_frequency;
                state.channel.tx_frequency = ui_state.new_tx_frequency;
                *sync_rtx = true;
                // play is called at end.
                vp_announceFrequencies(state.channel.rx_frequency,
                                       state.channel.tx_frequency, vpqInit);
            }

            state.ui_screen = MAIN_VFO;
        }
    }

    vp_play();
}

#ifdef CONFIG_SCREEN_BRIGHTNESS
static void _ui_changeBrightness(int variation)
{
    state.settings.brightness += variation;

    // Max value for brightness is 100, min value is set to 5 to avoid complete
    //  display shutdown.
    if(state.settings.brightness > 100) state.settings.brightness = 100;
    if(state.settings.brightness < 5)   state.settings.brightness = 5;

    display_setBacklightLevel(state.settings.brightness);
}
#endif

#ifdef CONFIG_SCREEN_CONTRAST
static void _ui_changeContrast(int variation)
{
    if(variation >= 0)
        state.settings.contrast =
        (255 - state.settings.contrast < variation) ? 255 : state.settings.contrast + variation;
    else
        state.settings.contrast =
        (state.settings.contrast < -variation) ? 0 : state.settings.contrast + variation;

    display_setContrast(state.settings.contrast);
}
#endif

static void _ui_changeTimer(int variation)
{
    if ((state.settings.display_timer == TIMER_OFF && variation < 0) ||
        (state.settings.display_timer == TIMER_1H && variation > 0))
    {
        return;
    }

    state.settings.display_timer += variation;
}

static void _ui_changeMacroLatch(bool newVal)
{
    state.settings.macroMenuLatch = newVal ? 1 : 0;
    vp_announceSettingsOnOffToggle(&currentLanguage->macroLatching,
                                   vp_getVoiceLevelQueueFlags(),
                                   state.settings.macroMenuLatch);
}

#ifdef CONFIG_M17
static inline void _ui_changeM17Can(int variation)
{
    uint8_t can = state.settings.m17_can;
    state.settings.m17_can = (can + variation) % 16;
}
#endif

static void _ui_changeVoiceLevel(int variation)
{
    if ((state.settings.vpLevel == vpNone && variation < 0) ||
        (state.settings.vpLevel == vpHigh && variation > 0))
        {
            return;
        }

    state.settings.vpLevel += variation;

    // Force these flags to ensure the changes are spoken for levels 1 through 3.
    enum vpQueueFlags flags = vpqInit
                         | vpqAddSeparatingSilence
                         | vpqPlayImmediately;

    if (!vp_isPlaying())
    {
        flags |= vpqIncludeDescriptions;
    }

    vp_announceSettingsVoiceLevel(flags);
}

static void _ui_changePhoneticSpell(bool newVal)
{
    state.settings.vpPhoneticSpell = newVal ? 1 : 0;

    vp_announceSettingsOnOffToggle(&currentLanguage->phonetic,
                                   vp_getVoiceLevelQueueFlags(),
                                   state.settings.vpPhoneticSpell);
}

bool _ui_checkStandby(long long time_since_last_event)
{
    if (standby)
    {
        return false;
    }

    switch (state.settings.display_timer)
    {
        case TIMER_OFF:
            return false;
        case TIMER_5S:
        case TIMER_10S:
        case TIMER_15S:
        case TIMER_20S:
        case TIMER_25S:
        case TIMER_30S:
            return time_since_last_event >= (5000 * state.settings.display_timer);
        case TIMER_1M:
        case TIMER_2M:
        case TIMER_3M:
        case TIMER_4M:
        case TIMER_5M:
            return time_since_last_event >=
                (60000 * (state.settings.display_timer - (TIMER_1M - 1)));
        case TIMER_15M:
        case TIMER_30M:
        case TIMER_45M:
            return time_since_last_event >=
                (60000 * 15 * (state.settings.display_timer - (TIMER_15M - 1)));
        case TIMER_1H:
            return time_since_last_event >= 60 * 60 * 1000;
    }

    // unreachable code
    return false;
}

static void _ui_enterStandby()
{
    if(standby)
        return;

    standby = true;
    redraw_needed = false;
    display_setBacklightLevel(0);
}

static bool _ui_exitStandby(long long now)
{
    last_event_tick = now;

    if(!standby)
        return false;

    standby = false;
    redraw_needed = true;
    display_setBacklightLevel(state.settings.brightness);

    return true;
}

// TODO: find a better home for this function
int _ui_handleToneSelectScroll(bool direction_up)
{
    bool tone_tx_enable = state.channel.fm.txToneEn;
    bool tone_rx_enable = state.channel.fm.rxToneEn;
    uint8_t tone_flags = tone_tx_enable << 1 | tone_rx_enable;

    if(direction_up)
        tone_flags++;
    else
        tone_flags--;

    tone_flags %= 4;
    tone_tx_enable = tone_flags >> 1;
    tone_rx_enable = tone_flags & 1;
    state.channel.fm.txToneEn = tone_tx_enable;
    state.channel.fm.rxToneEn = tone_rx_enable;

    return 1;
}

/**
 * Next radio mode: FM, M17, TETRA (the ones built in). Side key + 5, or a
 * long 5 on the main screens.
 */
static void _ui_cycleMode(bool *sync_rtx, enum vpQueueFlags queueFlags)
{
    static const uint8_t modes[] = {
        OPMODE_FM,
        #ifdef CONFIG_M17
        OPMODE_M17,
        #endif
        #ifdef CONFIG_TETRA
        OPMODE_TETRA,
        #endif
    };
    size_t count = sizeof(modes) / sizeof(modes[0]);
    size_t i = 0;

    // An unknown mode goes back to the first, so that it is not locked out
    while((i < count) && (modes[i] != state.channel.mode))
        i++;
    state.channel.mode = modes[(i + 1) % count];
    *sync_rtx = true;
    vp_announceRadioMode(state.channel.mode, queueFlags);
}

/**
 * @return true for a long press of 5 on its own, which changes the mode.
 */
static bool _ui_isModeKey(kbd_msg_t msg)
{
    return msg.long_press && (msg.keys == KEY_5);
}

/* Typing a memory channel number: jump after this long without a key */
#define CH_ENTRY_TIMEOUT_MS 2000
/* Digits of a memory channel number */
#define CH_ENTRY_MAX_DIGITS 4

/*
 * Number key pressed in memory mode: typed when released, so that holding
 * it for its function does not type it first.
 */
static keyboard_t memPendingKey;

static void _ui_chEntryCancel(void)
{
    ui_state.ch_entry = 0;
    ui_state.ch_entry_digits = 0;
}

/**
 * Jump to the memory channel typed, if there is one with that number.
 */
static void _ui_chEntryConfirm(bool *sync_rtx)
{
    uint16_t number = ui_state.ch_entry;

    _ui_chEntryCancel();
    if(number == 0)
        return;

    if((_ui_fsm_loadChannel(number - 1, sync_rtx) != -1)
       && (state.channel_index == number - 1))
    {
        vp_announceChannelName(&state.channel, state.channel_index + 1,
                               vp_getVoiceLevelQueueFlags());
    }
}

static void _ui_chEntryDigit(uint8_t digit, bool *sync_rtx)
{
    // No channel 0
    if((ui_state.ch_entry_digits == 0) && (digit == 0))
        return;

    ui_state.ch_entry = ui_state.ch_entry * 10 + digit;
    ui_state.ch_entry_digits++;
    ui_state.ch_entry_tick = getTick();
    vp_flush();
    vp_queueInteger(digit);
    vp_play();

    if(ui_state.ch_entry_digits >= CH_ENTRY_MAX_DIGITS)
        _ui_chEntryConfirm(sync_rtx);
}

static void _ui_fsm_menuMacro(kbd_msg_t msg, bool *sync_rtx);

/**
 * Number keys in memory mode:
 * - short press, on release: type a channel number; Enter jumps to it, ESC
 *   cancels, and it jumps on its own after CH_ENTRY_TIMEOUT_MS;
 * - long press: the function of side key + that number.
 * In T mode 0 on its own stays the recent calls key, see _ui_tetraKeys().
 *
 * @return true if the key was used.
 */
static bool _ui_memNumberKeys(kbd_msg_t msg, bool *sync_rtx)
{
    bool typing = ui_state.ch_entry_digits > 0;

    // Released before its long press: type it
    if((memPendingKey != 0) && ((msg.keys & memPendingKey) == 0))
    {
        _ui_chEntryDigit(__builtin_ctz(memPendingKey), sync_rtx);
        memPendingKey = 0;
        typing = ui_state.ch_entry_digits > 0;
        if(msg.keys == 0)
            return true;
    }

    keyboard_t number = msg.keys & KBD_NUM_MASK;
    bool oneNumber = (number != 0) && (msg.keys == number)
                  && ((number & (number - 1)) == 0);

    if(!oneNumber)
    {
        if(typing && (msg.keys & KEY_ENTER))
        {
            _ui_chEntryConfirm(sync_rtx);
            return true;
        }
        if(typing && (msg.keys & KEY_ESC))
        {
            _ui_chEntryCancel();
            return true;
        }
        // Any other key gives up the number and does its own thing
        if(msg.keys != 0)
            _ui_chEntryCancel();
        return false;
    }

    uint8_t digit = input_getPressedNumber(msg);

    #ifdef CONFIG_TETRA
    if((digit == 0) && !typing && (state.channel.mode == OPMODE_TETRA))
        return false;
    #endif

    if(msg.long_press)
    {
        memPendingKey = 0;
        if(digit == 0)
        {
            #ifdef CONFIG_SCAN
            // Long 0 in FM: scan; once a number is started, still a digit
            if(!typing && (state.channel.mode == OPMODE_FM))
            {
                _ui_scanStart(sync_rtx);
                return true;
            }
            #endif
            _ui_chEntryDigit(0, sync_rtx);
            return true;
        }
        _ui_chEntryCancel();
        _ui_fsm_menuMacro(msg, sync_rtx);
        return true;
    }

    memPendingKey = number;
    return true;
}

static void _ui_fsm_menuMacro(kbd_msg_t msg, bool *sync_rtx)
{
    // If there is no keyboard left and right select the menu entry to edit
#if defined(CONFIG_UI_NO_KEYBOARD)
    if (msg.keys & KNOB_LEFT)
    {
        ui_state.macro_menu_selected--;
        ui_state.macro_menu_selected += 9;
        ui_state.macro_menu_selected %= 9;
    }
    if (msg.keys & KNOB_RIGHT)
    {
        ui_state.macro_menu_selected++;
        ui_state.macro_menu_selected %= 9;
    }
    if ((msg.keys & KEY_ENTER) && !msg.long_press)
        ui_state.input_number = ui_state.macro_menu_selected + 1;
    else
        ui_state.input_number = 0;
#else // CONFIG_UI_NO_KEYBOARD
    ui_state.input_number = input_getPressedNumber(msg);
#endif // CONFIG_UI_NO_KEYBOARD
    // CTCSS Encode/Decode Selection
    enum vpQueueFlags queueFlags = vp_getVoiceLevelQueueFlags();

    switch(ui_state.input_number)
    {
        case 1:
            if(state.channel.mode == OPMODE_FM)
            {
                _ui_handleToneSelectScroll(true);
                *sync_rtx                 = true;
                vp_announceCTCSS(
                    state.channel.fm.rxToneEn, state.channel.fm.rxTone,
                    state.channel.fm.txToneEn, state.channel.fm.txTone,
                    queueFlags | vpqIncludeDescriptions);
            }
            break;
        case 2:
            if (state.channel.mode == OPMODE_FM)
            {
                if (state.channel.fm.txTone == 0)
                {
                    state.channel.fm.txTone = CTCSS_FREQ_NUM-1;
                }
                else
                {
                    state.channel.fm.txTone--;
                }

                state.channel.fm.txTone %= CTCSS_FREQ_NUM;
                state.channel.fm.rxTone = state.channel.fm.txTone;
                *sync_rtx = true;
                vp_announceCTCSS(state.channel.fm.rxToneEn,
                                 state.channel.fm.rxTone,
                                 state.channel.fm.txToneEn,
                                 state.channel.fm.txTone,
                                 queueFlags);
            }
            break;

        case 3:
            if(state.channel.mode == OPMODE_FM)
            {
                state.channel.fm.txTone++;
                state.channel.fm.txTone %= CTCSS_FREQ_NUM;
                state.channel.fm.rxTone = state.channel.fm.txTone;
                *sync_rtx = true;
                vp_announceCTCSS(state.channel.fm.rxToneEn,
                                 state.channel.fm.rxTone,
                                 state.channel.fm.txToneEn,
                                 state.channel.fm.txTone,
                                 queueFlags |vpqIncludeDescriptions);
            }
            break;
        case 4:
            if(state.channel.mode == OPMODE_FM)
            {
                state.channel.bandwidth++;
                state.channel.bandwidth %= 2;
                *sync_rtx = true;
                vp_announceBandwidth(state.channel.bandwidth, queueFlags);
            }
            break;
        case 5:
            _ui_cycleMode(sync_rtx, queueFlags);
            break;
        case 6:

            switch(state.channel.power)
            {
                case 1000:
                    state.channel.power = 2500;
                    break;

                case 2500:
                    state.channel.power = 5000;
                    break;

                default:
                    state.channel.power = 1000;
            }

            *sync_rtx = true;
            vp_announcePower(state.channel.power, queueFlags);
            break;
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case 7:
            _ui_changeBrightness(-5);
            vp_announceSettingsInt(&currentLanguage->brightness, queueFlags,
                                   state.settings.brightness);
            break;
        case 8:
            _ui_changeBrightness(+5);
            vp_announceSettingsInt(&currentLanguage->brightness, queueFlags,
                                   state.settings.brightness);
            break;
#endif
        case 9:
            if (!ui_state.input_locked)
                ui_state.input_locked = true;
            else
                ui_state.input_locked = false;
            break;
    }

#ifdef CONFIG_SPECTRUM
    // Side key + 0 in FM: the spectrum view
    bool mainScreen = (state.ui_screen == MAIN_VFO)
                   || (state.ui_screen == MAIN_MEM);
    if((msg.keys & KEY_0) && !msg.long_press && mainScreen
       && (state.channel.mode == OPMODE_FM))
    {
        macro_latched = false;
        macro_menu = false;
        _ui_spectrumOpen();
        return;
    }
#endif
#ifdef CONFIG_TETRA_LOG
    // Side key + 0: send the stored TETRA log over the cable
    if((msg.keys & KEY_0) && !msg.long_press)
        tetraLog_requestHistory();
#endif

#if defined(PLATFORM_TTWRPLUS)
    if(msg.keys & KEY_VOLDOWN)
#else
    if(msg.keys & KEY_LEFT || msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT)
#endif // PLATFORM_TTWRPLUS
    {
#ifdef CONFIG_KNOB_ABSOLUTE // If the radio has an absolute position knob
        state.settings.sqlLevel = platform_getChSelector() - 1;
#endif // CONFIG_KNOB_ABSOLUTE
        if(state.settings.sqlLevel > 0)
        {
            state.settings.sqlLevel -= 1;
            *sync_rtx = true;
            vp_announceSquelch(state.settings.sqlLevel, queueFlags);
        }
    }

#if defined(PLATFORM_TTWRPLUS)
    else if(msg.keys & KEY_VOLUP)
#else
    else if(msg.keys & KEY_RIGHT || msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)
#endif // PLATFORM_TTWRPLUS
    {
#ifdef CONFIG_KNOB_ABSOLUTE
        state.settings.sqlLevel = platform_getChSelector() - 1;
#endif
        if(state.settings.sqlLevel < 15)
        {
            state.settings.sqlLevel += 1;
            *sync_rtx = true;
            vp_announceSquelch(state.settings.sqlLevel, queueFlags);
        }
    }
}

static void _ui_menuUp(uint8_t menu_entries)
{
    if(ui_state.menu_selected > 0)
        ui_state.menu_selected -= 1;
    else
        ui_state.menu_selected = menu_entries - 1;
    vp_playMenuBeepIfNeeded(ui_state.menu_selected==0);
}

static void _ui_menuDown(uint8_t menu_entries)
{
    if(ui_state.menu_selected < menu_entries - 1)
        ui_state.menu_selected += 1;
    else
        ui_state.menu_selected = 0;
    vp_playMenuBeepIfNeeded(ui_state.menu_selected==0);
}

static void _ui_menuBack(uint8_t prev_state)
{
    if(ui_state.edit_mode)
    {
        ui_state.edit_mode = false;
    }
    else
    {
        // Return to previous menu
        state.ui_screen = prev_state;
        // Reset menu selection
        ui_state.menu_selected = 0;
        vp_playMenuBeepIfNeeded(true);
    }
}

static void _ui_textInputReset(char *buf, size_t bufSize)
{
    ui_state.input_number = 0;
    ui_state.input_position = 0;
    ui_state.input_set = 0;
    ui_state.last_keypress = 0;
    memset(buf, 0, bufSize);
    buf[0] = '_';
}

static void _ui_textInputKeypad(char *buf, uint8_t max_len, kbd_msg_t msg,
                         bool callsign)
{
    long long now = getTick();
    // Get currently pressed number key
    uint8_t num_key = input_getPressedChar(msg);

    bool key_timeout = ((now - ui_state.last_keypress) >= input_longPressTimeout);
    bool same_key = ui_state.input_number == num_key;
    // Get number of symbols related to currently pressed key
    uint8_t num_symbols = 0;
    if(callsign)
    {
        num_symbols = strlen(symbols_ITU_T_E161_callsign[num_key]);
        if(num_symbols == 0)
            return;
    }
    else
        num_symbols = strlen(symbols_ITU_T_E161[num_key]);

    // Return if max length is reached or finished editing last character
    if((ui_state.input_position >= max_len) || ((ui_state.input_position == (max_len-1)) && (key_timeout || !same_key)))
        return;

    // Skip keypad logic for first keypress
    if(ui_state.last_keypress != 0)
    {
        // Same key pressed and timeout not expired: cycle over chars of current key
        if(same_key && !key_timeout)
        {
            ui_state.input_set = (ui_state.input_set + 1) % num_symbols;
        }
        // Different key pressed: save current char and change key
        else
        {
            ui_state.input_position += 1;
            ui_state.input_set = 0;
        }
    }
    // Show current character on buffer
    if(callsign)
        buf[ui_state.input_position] = symbols_ITU_T_E161_callsign[num_key][ui_state.input_set];
    else
    {
        buf[ui_state.input_position] = symbols_ITU_T_E161[num_key][ui_state.input_set];
    }
    // Announce the character
    vp_announceInputChar(buf[ui_state.input_position]);
    // Update reference values
    ui_state.input_number = num_key;
    ui_state.last_keypress = now;
}

static void _ui_textInputConfirm(char *buf)
{
    buf[ui_state.input_position + 1] = '\0';
}

static void _ui_textInputDel(char *buf)
{
    // announce the char about to be backspaced.
    // Note this assumes editing callsign.
    // If we edit a different buffer which allows the underline char, we may
    // not want to exclude it, but when editing callsign, we do not want to say
    // underline since it means the field is empty.
    if(buf[ui_state.input_position]
    && buf[ui_state.input_position]!='_')
        vp_announceInputChar(buf[ui_state.input_position]);

    buf[ui_state.input_position] = '\0';
    // Move back input cursor
    if(ui_state.input_position > 0)
    {
        ui_state.input_position--;
    // If we deleted the initial character, reset starting condition
    }
    else
        ui_state.last_keypress = 0;
    ui_state.input_set = 0;
}

static void _ui_numberInputKeypad(uint32_t *num, kbd_msg_t msg)
{
    long long now = getTick();

#ifdef CONFIG_UI_NO_KEYBOARD
    // If knob is turned, increment or Decrement
    if (msg.keys & KNOB_LEFT)
    {
        *num = *num + 1;
        if (*num % 10 == 0)
            *num = *num - 10;
    }

    if (msg.keys & KNOB_RIGHT)
    {
        if (*num == 0)
            *num = 9;
        else
        {
            *num = *num - 1;
            if (*num % 10 == 9)
                *num = *num + 10;
        }
    }

    // If enter is pressed, advance to the next digit
    if (msg.keys & KEY_ENTER)
    {
        uint32_t tmp = 0;
        if (!__builtin_mul_overflow(*num, 10, &tmp))
            *num = tmp;
    }

    // Announce the character
    vp_announceInputChar('0' + *num % 10);

    // Update reference values
    ui_state.input_number = *num % 10;
#else
    // Get currently pressed number key
    uint8_t num_key = input_getPressedNumber(msg);

    uint32_t tmp = 0;

    // Add a another digit onto the offset only if
    // it won't cause an overflow
    if (__builtin_mul_overflow(*num, 10, &tmp) ||
        __builtin_add_overflow(tmp, num_key, &tmp))
        return;

    *num = tmp;

    // Announce the character
    vp_announceInputChar('0' + num_key);

    // Update reference values
    ui_state.input_number = num_key;
#endif

    ui_state.last_keypress = now;
}

static void _ui_numberInputDel(uint32_t *num)
{
    // announce the digit about to be backspaced.
    vp_announceInputChar('0' + *num % 10);

    // Move back input cursor
    if(ui_state.input_position > 0)
        ui_state.input_position--;
    else
        ui_state.last_keypress = 0;

    ui_state.input_set = 0;
}

void ui_init()
{
    last_event_tick = getTick();
    redraw_needed = true;
    _ui_calculateLayout(&layout);
    layout_ready = true;
    // Initialize struct ui_state to all zeroes
    // This syntax is called compound literal
    // https://stackoverflow.com/questions/6891720/initialize-reset-struct-to-zero-null
    ui_state = (const struct ui_state_t){ 0 };

#ifdef CONFIG_TETRA
    // TETRA settings, and the talkgroup filter of the last power off
    _ui_tetraApplySettings();
    if (!state.settings.tetraFilterReset
        && state.settings.tetraFilter < tetraIds_filterCount()) {
        for (uint8_t i = 0; i < state.settings.tetraFilter; i++)
            tetraIds_stepFilter(1);
    }
#endif
}

void ui_drawSplashScreen()
{
    gfx_clearScreen();

    #if CONFIG_SCREEN_HEIGHT > 64
    static const point_t    logo_orig = {0, (CONFIG_SCREEN_HEIGHT / 2) - 6};
    static const point_t    call_orig = {0, CONFIG_SCREEN_HEIGHT - 8};
    static const fontSize_t logo_font = FONT_SIZE_12PT;
    static const fontSize_t call_font = FONT_SIZE_8PT;
    #else
    static const point_t    logo_orig = {0, 19};
    static const point_t    call_orig = {0, CONFIG_SCREEN_HEIGHT - 8};
    static const fontSize_t logo_font = FONT_SIZE_8PT;
    static const fontSize_t call_font = FONT_SIZE_6PT;
    #endif

    gfx_print(logo_orig, logo_font, TEXT_ALIGN_CENTER, yellow_fab413, "O P N\nR T X");
    gfx_print(call_orig, call_font, TEXT_ALIGN_CENTER, color_white, state.settings.callsign);

    vp_announceSplashScreen();
}

void ui_saveState()
{
    last_state = state;
}

#ifdef CONFIG_GPS
static uint16_t priorGPSSpeed = 0;
static int16_t  priorGPSAltitude = 0;
static int16_t  priorGPSDirection = 500; // impossible value init.
static uint8_t  priorGPSFixQuality= 0;
static uint8_t  priorGPSFixType = 0;
static uint8_t  priorSatellitesInView = 0;
static uint32_t vpGPSLastUpdate = 0;

static enum vpGPSInfoFlags GetGPSDirectionOrSpeedChanged()
{
    if (!state.settings.gps_enabled)
        return vpGPSNone;

    uint32_t now = getTick();
    if (now - vpGPSLastUpdate < 8000)
        return vpGPSNone;

    enum vpGPSInfoFlags whatChanged = vpGPSNone;

    if (state.gps_data.fix_quality != priorGPSFixQuality)
    {
        whatChanged |= vpGPSFixQuality;
        priorGPSFixQuality= state.gps_data.fix_quality;
    }

    if (state.gps_data.fix_type != priorGPSFixType)
    {
        whatChanged |= vpGPSFixType;
        priorGPSFixType = state.gps_data.fix_type;
    }

    if (state.gps_data.speed != priorGPSSpeed)
    {
        whatChanged |= vpGPSSpeed;
        priorGPSSpeed = state.gps_data.speed;
    }

    if (state.gps_data.altitude != priorGPSAltitude)
    {
        whatChanged |= vpGPSAltitude;
        priorGPSAltitude = state.gps_data.altitude;
    }

    if (state.gps_data.tmg_true != priorGPSDirection)
    {
        whatChanged |= vpGPSDirection;
        priorGPSDirection = state.gps_data.tmg_true;
    }

    if (state.gps_data.satellites_in_view != priorSatellitesInView)
    {
        whatChanged |= vpGPSSatCount;
        priorSatellitesInView = state.gps_data.satellites_in_view;
    }

    if (whatChanged)
        vpGPSLastUpdate=now;

    return whatChanged;
}
#endif // CONFIG_GPS

void ui_updateFSM(bool *sync_rtx)
{
    // Check for events
    if(evQueue_wrPos == evQueue_rdPos) return;

    // Pop an event from the queue
    uint8_t newTail = (evQueue_rdPos + 1) % MAX_NUM_EVENTS;
    event_t event   = evQueue[evQueue_rdPos];
    evQueue_rdPos   = newTail;

    // There is some event to process, we need an UI redraw.
    // UI redraw request is cancelled if we're in standby mode.
    redraw_needed = true;
    if(standby) redraw_needed = false;

    // Check if battery has enough charge to operate.
    // Check is skipped if there is an ongoing transmission, since the voltage
    // drop caused by the RF PA power absorption causes spurious triggers of
    // the low battery alert.
    bool txOngoing = platform_getPttStatus();
#if !defined(PLATFORM_TTWRPLUS)
    if ((!state.emergency) && (!txOngoing) && (state.charge <= 0))
    {
        state.ui_screen = LOW_BAT;
        if(event.type == EVENT_KBD && event.payload)
        {
            state.ui_screen = MAIN_VFO;
            state.emergency = true;
        }
        return;
    }
#endif // PLATFORM_TTWRPLUS

    // Unlatch and exit from macro menu on PTT press
    if(macro_latched && txOngoing)
    {
        macro_latched = false;
        macro_menu = false;
    }

    long long now = getTick();
    // Process pressed keys
    if(event.type == EVENT_KBD)
    {
        kbd_msg_t msg;
        msg.value = event.payload;
        bool f1Handled = false;
        enum vpQueueFlags queueFlags = vp_getVoiceLevelQueueFlags();
        // If we get out of standby, we ignore the kdb event
        // unless is the MONI key for the MACRO functions
        if (_ui_exitStandby(now) && !(msg.keys & KEY_MONI))
            return;
#ifdef CONFIG_SELFTEST
        // The self test takes every key, the side key too
        if(state.ui_screen == SELF_TEST)
        {
            _ui_selfTestKey(msg);
            return;
        }
#endif
        // If MONI is pressed, activate MACRO functions
        bool moniPressed = msg.keys & KEY_MONI;
        if(moniPressed || macro_latched)
        {
            macro_menu = true;

            if(state.settings.macroMenuLatch == 1)
            {
                // long press moni on its own latches function.
                if (moniPressed && msg.long_press && !macro_latched)
                {
                    macro_latched = true;
                    vp_beep(BEEP_FUNCTION_LATCH_ON, LONG_BEEP);
                }
                else if (moniPressed && macro_latched)
                {
                    macro_latched = false;
                    vp_beep(BEEP_FUNCTION_LATCH_OFF, LONG_BEEP);
                }
            }

            _ui_fsm_menuMacro(msg, sync_rtx);
            return;
        }
        else
        {
            macro_menu = false;
        }
#if defined(PLATFORM_TTWRPLUS)
        // T-TWR Plus has no KEY_MONI, using KEY_VOLDOWN long press instead
        if ((msg.keys & KEY_VOLDOWN) && msg.long_press)
        {
            macro_menu = true;
            macro_latched = true;
        }
#endif // PLA%FORM_TTWRPLUS

        if(state.tone_enabled && !(msg.keys & KEY_HASH))
        {
            state.tone_enabled = false;
            *sync_rtx = true;
        }

#ifdef CONFIG_DTMF
        // Keypad keys while transmitting: DTMF
        if(_ui_dtmfKey(msg))
            return;
#endif
#ifdef CONFIG_REPLAY
        // P2: instant replay
        if(_ui_replayKey(msg))
            return;
#endif

        int priorUIScreen = state.ui_screen;
        switch(state.ui_screen)
        {
            // VFO screen
            case MAIN_VFO:
            {
                // Enable Tx in MAIN_VFO mode
                if (state.txDisable)
                {
                    state.txDisable = false;
                    *sync_rtx = true;
                }

                // Break out of the FSM if the keypad is locked but allow the
                // use of the hash key in FM mode for the 1750Hz tone.
                bool skipLock =  (state.channel.mode == OPMODE_FM)
                              && (msg.keys == KEY_HASH);

                if ((ui_state.input_locked == true) && (skipLock == false))
                    break;

                if(ui_state.edit_mode)
                {
                    #ifdef CONFIG_M17
                    if(state.channel.mode == OPMODE_M17)
                    {
                        if(msg.keys & KEY_ENTER)
                        {
                            _ui_textInputConfirm(ui_state.new_callsign);
                            // Save selected dst ID and disable input mode
                            strncpy(state.settings.m17_dest, ui_state.new_callsign, 10);
                            ui_state.edit_mode = false;
                            *sync_rtx = true;
                            vp_announceM17Info(NULL,  ui_state.edit_mode,
                                               queueFlags);
                        }
                        else if(msg.keys & KEY_HASH)
                        {
                            // Save selected dst ID and disable input mode
                            strncpy(state.settings.m17_dest, "", 1);
                            ui_state.edit_mode = false;
                            *sync_rtx = true;
                            vp_announceM17Info(NULL,  ui_state.edit_mode,
                                               queueFlags);
                        }
                        else if(msg.keys & KEY_ESC)
                            // Discard selected dst ID and disable input mode
                            ui_state.edit_mode = false;
                        else if(msg.keys & KEY_UP || msg.keys & KEY_DOWN ||
                                msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT)
                            _ui_textInputDel(ui_state.new_callsign);
                        else if(input_isCharPressed(msg))
                            _ui_textInputKeypad(ui_state.new_callsign, 9, msg, true);
                        break;
                    }
                    #endif
                }
                else
                {
                    #ifdef CONFIG_SCAN
                    if(_ui_scanKey(msg, sync_rtx))
                    {
                        // Scanning: up, down, or stop
                    }
                    else
                    #endif
                    #ifdef CONFIG_TETRA
                    if(_ui_tetraKeys(msg))
                    {
                        // TETRA filter, hold, line 1 or recent calls
                    }
                    else
                    #endif
                    #ifdef CONFIG_FAST_COPY
                    if(_ui_fastCopyKey(msg))
                    {
                        // Long *: read a nearby radio
                    }
                    else
                    #endif
                    if(msg.keys & KEY_ENTER)
                    {
                        // Save current main state
                        ui_state.last_main_state = state.ui_screen;
                        // Open Menu
                        state.ui_screen = MENU_TOP;
                        // The selected item will be announced when the item is first selected.
                    }
                    else if(msg.keys & KEY_ESC)
                    {
                        // Save VFO channel
                        state.vfo_channel = state.channel;
                        int result = _ui_fsm_loadChannel(state.channel_index, sync_rtx);
                        // Read successful and channel is valid
                        if(result != -1)
                        {
                            // Switch to MEM screen
                            state.ui_screen = MAIN_MEM;
                            // anounce the active channel name.
                            vp_announceChannelName(&state.channel,
                                                   state.channel_index,
                                                   queueFlags);
                        }
                    }
                    else if(msg.keys & KEY_HASH)
                    {
                        #ifdef CONFIG_M17
                        // Only enter edit mode when using M17
                        if(state.channel.mode == OPMODE_M17)
                        {
                            // Enable dst ID input
                            ui_state.edit_mode = true;
                            // Reset text input variables
                            _ui_textInputReset(ui_state.new_callsign,
                                    sizeof(ui_state.new_callsign));
                            vp_announceM17Info(NULL,  ui_state.edit_mode,
                                               queueFlags);
                        }
                        else
                        #endif
                        {
                            if(!state.tone_enabled)
                            {
                                state.tone_enabled = true;
                                *sync_rtx = true;
                            }
                        }
                    }
                    else if(msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)
                    {
                        // Increment TX and RX frequency of 12.5KHz
                        if(_ui_freq_check_limits(state.channel.rx_frequency + freq_steps[state.step_index]) &&
                           _ui_freq_check_limits(state.channel.tx_frequency + freq_steps[state.step_index]))
                        {
                            state.channel.rx_frequency += freq_steps[state.step_index];
                            state.channel.tx_frequency += freq_steps[state.step_index];
                            *sync_rtx = true;
                            vp_announceFrequencies(state.channel.rx_frequency,
                                                   state.channel.tx_frequency,
                                                   queueFlags);
                        }
                    }
                    else if(msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT)
                    {
                        // Decrement TX and RX frequency of 12.5KHz
                        if(_ui_freq_check_limits(state.channel.rx_frequency - freq_steps[state.step_index]) &&
                           _ui_freq_check_limits(state.channel.tx_frequency - freq_steps[state.step_index]))
                        {
                            state.channel.rx_frequency -= freq_steps[state.step_index];
                            state.channel.tx_frequency -= freq_steps[state.step_index];
                            *sync_rtx = true;
                            vp_announceFrequencies(state.channel.rx_frequency,
                                                   state.channel.tx_frequency,
                                                   queueFlags);
                        }
                    }
                    else if(msg.keys & KEY_F1)
                    {
                        if (state.settings.vpLevel > vpBeep)
                        {// quick press repeat vp, long press summary.
                            if (msg.long_press)
                                vp_announceChannelSummary(&state.channel, 0,
                                                          state.bank, vpAllInfo);
                            else
                                vp_replayLastPrompt();
                            f1Handled = true;
                        }
                    }
                    else if(input_isNumberPressed(msg))
                    {
                        // Open Frequency input screen
                        state.ui_screen = MAIN_VFO_INPUT;
                        // Reset input position and selection
                        ui_state.input_position = 1;
                        ui_state.input_set = SET_RX;
                        // do not play  because we will also announce the number just entered.
                        vp_announceInputReceiveOrTransmit(false, vpqInit);
                        vp_queueInteger(input_getPressedNumber(msg));
                        vp_play();

                        ui_state.new_rx_frequency = 0;
                        ui_state.new_tx_frequency = 0;
                        // Save pressed number to calculare frequency and show in GUI
                        ui_state.input_number = input_getPressedNumber(msg);
                        // Calculate portion of the new frequency
                        ui_state.new_rx_frequency = _ui_freq_add_digit(ui_state.new_rx_frequency,
                                                                       ui_state.input_position,
                                                                       ui_state.input_number);
                    }
                }
            }
                break;
            // VFO frequency input screen
            case MAIN_VFO_INPUT:
                // Only the 5 typed, and held: change mode, not frequency
                if(_ui_isModeKey(msg) && (ui_state.input_set == SET_RX)
                   && (ui_state.input_position == 1)
                   && (ui_state.input_number == 5))
                {
                    state.ui_screen = MAIN_VFO;
                    _ui_cycleMode(sync_rtx, queueFlags);
                }
                #ifdef CONFIG_SCAN
                // Only the 0 typed, and held: scan in FM (no frequency
                // starts with 0)
                else if(msg.long_press && (msg.keys == KEY_0)
                        && (ui_state.input_set == SET_RX)
                        && (ui_state.input_position == 1)
                        && (ui_state.input_number == 0)
                        && (state.channel.mode == OPMODE_FM))
                {
                    state.ui_screen = MAIN_VFO;
                    _ui_scanStart(sync_rtx);
                }
                #endif
                else if(msg.keys & KEY_ENTER)
                {
                    _ui_fsm_confirmVFOInput(sync_rtx);
                }
                else if(msg.keys & KEY_ESC)
                {
                    // Cancel frequency input, return to VFO mode
                    state.ui_screen = MAIN_VFO;
                }
                else if(msg.keys & KEY_UP || msg.keys & KEY_DOWN)
                {
                    if(ui_state.input_set == SET_RX)
                    {
                        ui_state.input_set = SET_TX;
                        vp_announceInputReceiveOrTransmit(true, queueFlags);
                    }
                    else if(ui_state.input_set == SET_TX)
                    {
                        ui_state.input_set = SET_RX;
                        vp_announceInputReceiveOrTransmit(false, queueFlags);
                    }
                    // Reset input position
                    ui_state.input_position = 0;
                }
                else if(input_isNumberPressed(msg))
                {
                    _ui_fsm_insertVFONumber(msg, sync_rtx);
                }
                break;
            // MEM screen
            case MAIN_MEM:
                // Enable Tx in MAIN_MEM mode
                if (state.txDisable)
                {
                    state.txDisable = false;
                    *sync_rtx = true;
                }
                // Long 9 unlocks the keypad, as side key + 9 does
                if (ui_state.input_locked && msg.long_press
                    && (msg.keys == KEY_9))
                {
                    ui_state.input_locked = false;
                    break;
                }
                if (ui_state.input_locked)
                    break;
                // M17 Destination callsign input
                if(ui_state.edit_mode)
                {
                    {
                        if(msg.keys & KEY_ENTER)
                        {
                            _ui_textInputConfirm(ui_state.new_callsign);
                            // Save selected dst ID and disable input mode
                            strncpy(state.settings.m17_dest, ui_state.new_callsign, 10);
                            ui_state.edit_mode = false;
                            *sync_rtx = true;
                        }
                        else if(msg.keys & KEY_HASH)
                        {
                            // Save selected dst ID and disable input mode
                            strncpy(state.settings.m17_dest, "", 1);
                            ui_state.edit_mode = false;
                            *sync_rtx = true;
                        }
                        else if(msg.keys & KEY_ESC)
                            // Discard selected dst ID and disable input mode
                            ui_state.edit_mode = false;
                        else if(msg.keys & KEY_F1)
                        {
                            if (state.settings.vpLevel > vpBeep)
                            {
                                // Quick press repeat vp, long press summary.
                                if (msg.long_press)
                                {
                                    vp_announceChannelSummary(
                                            &state.channel,
                                            state.channel_index,
                                            state.bank,
                                            vpAllInfo);
                                }
                                else
                                {
                                    vp_replayLastPrompt();
                                }

                                f1Handled = true;
                            }
                        }
                        else if(msg.keys & KEY_UP || msg.keys & KEY_DOWN ||
                                msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT)
                            _ui_textInputDel(ui_state.new_callsign);
                        else if(input_isCharPressed(msg))
                            _ui_textInputKeypad(ui_state.new_callsign, 9, msg, true);
                        break;
                    }
                }
                else
                {
                    #ifdef CONFIG_SCAN
                    if(_ui_scanKey(msg, sync_rtx))
                    {
                        // Scanning: up, down, or stop
                    }
                    else
                    #endif
                    if(_ui_memNumberKeys(msg, sync_rtx))
                    {
                        // Channel number, or a long press function
                    }
                    else
                    #ifdef CONFIG_TETRA
                    if(_ui_tetraKeys(msg))
                    {
                        // TETRA filter, hold, line 1 or recent calls
                    }
                    else
                    #endif
                    #ifdef CONFIG_FAST_COPY
                    if(_ui_fastCopyKey(msg))
                    {
                        // Long *: read a nearby radio
                    }
                    else
                    #endif
                    if(msg.keys & KEY_ENTER)
                    {
                        // Save current main state
                        ui_state.last_main_state = state.ui_screen;
                        // Open Menu
                        state.ui_screen = MENU_TOP;
                    }
                    else if(msg.keys & KEY_ESC)
                    {
                        // Restore VFO channel
                        state.channel = state.vfo_channel;
                        // Update RTX configuration
                        *sync_rtx = true;
                        // Switch to VFO screen
                        state.ui_screen = MAIN_VFO;
                    }
                    else if(msg.keys & KEY_HASH)
                    {
                        // Only enter edit mode when using M17
                        if(state.channel.mode == OPMODE_M17)
                        {
                            // Enable dst ID input
                            ui_state.edit_mode = true;
                            // Reset text input variables
                            _ui_textInputReset(ui_state.new_callsign,
                                    sizeof(ui_state.new_callsign));
                        }
                        else
                        {
                            if(!state.tone_enabled)
                            {
                                state.tone_enabled = true;
                                *sync_rtx = true;
                            }
                        }
                    }
                    else if(msg.keys & KEY_F1)
                    {
                        if (state.settings.vpLevel > vpBeep)
                        {// quick press repeat vp, long press summary.
                            if (msg.long_press)
                            {
                                vp_announceChannelSummary(&state.channel,
                                                          state.channel_index+1,
                                                          state.bank, vpAllInfo);
                            }
                            else
                            {
                                vp_replayLastPrompt();
                            }

                            f1Handled = true;
                        }
                    }
                    else if(msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)
                    {
                        _ui_fsm_loadChannel(state.channel_index + 1, sync_rtx);
                        vp_announceChannelName(&state.channel,
                                               state.channel_index+1,
                                               queueFlags);
                    }
                    else if(msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT)
                    {
                        _ui_fsm_loadChannel(state.channel_index - 1, sync_rtx);
                        vp_announceChannelName(&state.channel,
                                               state.channel_index+1,
                                               queueFlags);
                    }
                }
                break;
            // Top menu screen
            #ifdef CONFIG_SPECTRUM
            case SPECTRUM:
                _ui_spectrumKey(msg, sync_rtx);
                break;
            #endif
            #ifdef CONFIG_FAST_COPY
            // Fast copy: green copies what was found, red stops
            case FAST_COPY:
                if(msg.long_press)
                    break;
                if(msg.keys & KEY_ENTER)
                {
                    struct fastCopyResult r;
                    fastCopy_get(&r);
                    if(r.state == FAST_COPY_FOUND)
                    {
                        _ui_fastCopyApply(sync_rtx);
                        fastCopy_stop();
                    }
                }
                else if(msg.keys & KEY_ESC)
                {
                    fastCopy_stop();
                    state.ui_screen = ui_state.last_main_state;
                }
                break;
            #endif
            #ifdef CONFIG_TETRA
            // TETRA recent calls: Enter holds the selected one
            case TETRA_RECENT:
                if(msg.long_press)
                    break;
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                {
                    if(ui_state.menu_selected > 0)
                        ui_state.menu_selected--;
                }
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                {
                    if(_ui_tetraRecentSsi(ui_state.menu_selected + 1) != 0)
                        ui_state.menu_selected++;
                }
                else if(msg.keys & KEY_ENTER)
                {
                    if(_ui_tetraRecentSsi(ui_state.menu_selected) != 0)
                        state.ui_screen = TETRA_RECENT_DETAIL;
                }
                else if(msg.keys & (KEY_ESC | KEY_0))
                {
                    state.ui_screen = ui_state.last_main_state;
                }
                break;
            // One recent call in detail: Enter holds it
            case TETRA_RECENT_DETAIL:
                if(msg.long_press)
                    break;
                if(msg.keys & KEY_ENTER)
                {
                    tetraIds_hold(_ui_tetraRecentSsi(ui_state.menu_selected));
                    state.ui_screen = ui_state.last_main_state;
                }
                else if(msg.keys & KEY_ESC)
                {
                    state.ui_screen = TETRA_RECENT;
                }
                break;
            // TETRA field test: scrolls; Enter searches for the best signal
            case TETRA_FIELD_TEST:
                if(msg.long_press)
                    break;
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                {
                    if(ui_state.menu_selected > 0)
                        ui_state.menu_selected--;
                }
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                {
                    ui_state.menu_selected++;
                }
                else if(msg.keys & KEY_ENTER)
                {
                    tetraStatus_requestSearch();
                }
                else if(msg.keys & KEY_ESC)
                {
                    _ui_menuBack(MENU_TOP);
                }
                break;
            #endif
            case MENU_TOP:
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(menu_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(menu_num);
                else if(msg.keys & KEY_ENTER)
                {
                    switch(ui_state.menu_selected)
                    {
                        case M_BANK:
                            state.ui_screen = MENU_BANK;
                            break;
                        case M_CHANNEL:
                            state.ui_screen = MENU_CHANNEL;
                            break;
                        case M_CONTACTS:
                            state.ui_screen = MENU_CONTACTS;
                            break;
#ifdef CONFIG_GPS
                        case M_GPS:
                            state.ui_screen = MENU_GPS;
                            break;
#endif
#ifdef CONFIG_BCAST_FM
                        case M_BCAST:
                            _ui_bcastOpen(sync_rtx);
                            break;
#endif
#ifdef CONFIG_TETRA
                        case M_MESSAGES:
                            ui_state.msg_sel = 0;
                            state.ui_screen = TETRA_MESSAGES;
                            break;
                        case M_ACTIVITY:
                            ui_state.activity_sel = 0;
                            state.ui_screen = TETRA_ACTIVITY;
                            break;
#endif
#ifdef CONFIG_TETRA_CALLREC
                        case M_SAVED_CALLS:
                            ui_state.saved_sel = 0;
                            ui_state.saved_delete_armed = false;
                            state.ui_screen = TETRA_SAVED;
                            break;
#endif
                        case M_SETTINGS:
                            state.ui_screen = MENU_SETTINGS;
                            break;
                        case M_INFO:
                            state.ui_screen = MENU_INFO;
                            break;
#ifdef CONFIG_SELFTEST
                        case M_SELF_TEST:
                            _ui_selfTestOpen();
                            break;
#endif
#ifdef CONFIG_TETRA
                        case M_FIELD_TEST:
                            state.ui_screen = TETRA_FIELD_TEST;
                            break;
#endif
                        case M_ABOUT:
                            state.ui_screen = MENU_ABOUT;
                            break;
                    }
                    // Reset menu selection
                    ui_state.menu_selected = 0;
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(ui_state.last_main_state);
                break;
            // Zone menu screen
            case MENU_BANK:
            // Channel menu screen
            case MENU_CHANNEL:
            // Contacts menu screen
            case MENU_CONTACTS:
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    // Using 1 as parameter disables menu wrap around
                    _ui_menuUp(1);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                {
                    if(state.ui_screen == MENU_BANK)
                    {
                        bankHdr_t bank;
                        // manu_selected is 0-based
                        // bank 0 means "All Channel" mode
                        // banks (1, n) are mapped to banks (0, n-1)
                        if(cps_readBankHeader(&bank, ui_state.menu_selected) != -1)
                            ui_state.menu_selected += 1;
                    }
                    else if(state.ui_screen == MENU_CHANNEL)
                    {
                        channel_t channel;
                        if(cps_readChannel(&channel, ui_state.menu_selected + 1) != -1)
                            ui_state.menu_selected += 1;
                    }
                    else if(state.ui_screen == MENU_CONTACTS)
                    {
                        contact_t contact;
                        if(cps_readContact(&contact, ui_state.menu_selected + 1) != -1)
                            ui_state.menu_selected += 1;
                    }
                }
                else if(msg.keys & KEY_ENTER)
                {
                    if(state.ui_screen == MENU_BANK)
                    {
                        bankHdr_t newbank;
                        int result = 0;
                        // If "All channels" is selected, load default bank
                        if(ui_state.menu_selected == 0)
                            state.bank_enabled = false;
                        else
                        {
                            state.bank_enabled = true;
                            result = cps_readBankHeader(&newbank, ui_state.menu_selected - 1);
                        }
                        if(result != -1)
                        {
                            state.bank = ui_state.menu_selected - 1;
                            // If we were in VFO mode, save VFO channel
                            if(ui_state.last_main_state == MAIN_VFO)
                                state.vfo_channel = state.channel;
                            // Load bank first channel
                            _ui_fsm_loadChannel(0, sync_rtx);
                            // Switch to MEM screen
                            state.ui_screen = MAIN_MEM;
                        }
                    }
                    if(state.ui_screen == MENU_CHANNEL)
                    {
                        // If we were in VFO mode, save VFO channel
                        if(ui_state.last_main_state == MAIN_VFO)
                            state.vfo_channel = state.channel;
                        _ui_fsm_loadChannel(ui_state.menu_selected, sync_rtx);
                        // Switch to MEM screen
                        state.ui_screen = MAIN_MEM;
                    }
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
#ifdef CONFIG_GPS
            // GPS menu screen
            case MENU_GPS:
                if ((msg.keys & KEY_F1) && (state.settings.vpLevel > vpBeep))
                {// quick press repeat vp, long press summary.
                    if (msg.long_press)
                        vp_announceGPSInfo(vpGPSAll);
                    else
                        vp_replayLastPrompt();
                    f1Handled = true;
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
#endif
            // Settings menu screen
            case MENU_SETTINGS:
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_num);
                else if(msg.keys & KEY_ENTER)
                {

                    switch(ui_state.menu_selected)
                    {
                        case S_DISPLAY:
                            state.ui_screen = SETTINGS_DISPLAY;
                            break;
#ifdef CONFIG_RTC
                        case S_TIMEDATE:
                            state.ui_screen = SETTINGS_TIMEDATE;
                            break;
#endif
#ifdef CONFIG_GPS
                        case S_GPS:
                            state.ui_screen = SETTINGS_GPS;
                            break;
#endif
                        case S_RADIO:
                            state.ui_screen = SETTINGS_RADIO;
                            break;
#ifdef CONFIG_M17
                        case S_M17:
                            state.ui_screen = SETTINGS_M17;
                            break;
#endif
                        case S_FM:
                            state.ui_screen = SETTINGS_FM;
                            break;
#ifdef CONFIG_MEMCH
                        case S_MEMORY:
                            state.ui_screen = SETTINGS_MEMORY;
                            ui_state.mem_status = NULL;
                            ui_state.mem_delete_armed = false;
                            ui_state.mem_naming = false;
                            break;
#endif
#ifdef CONFIG_TETRA
                        case S_TETRA:
                            state.ui_screen = SETTINGS_TETRA;
                            break;
#endif
                        case S_ACCESSIBILITY:
                            state.ui_screen = SETTINGS_ACCESSIBILITY;
                            break;
                        case S_RESET2DEFAULTS:
                            state.ui_screen = SETTINGS_RESET2DEFAULTS;
                            break;
                        default:
                            state.ui_screen = MENU_SETTINGS;
                    }
                    // Reset menu selection
                    ui_state.menu_selected = 0;
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
            // Flash backup and restore menu screen
            case MENU_BACKUP_RESTORE:
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_num);
                else if(msg.keys & KEY_ENTER)
                {

                    switch(ui_state.menu_selected)
                    {
                        case BR_BACKUP:
                            state.ui_screen = MENU_BACKUP;
                            break;
                        case BR_RESTORE:
                            state.ui_screen = MENU_RESTORE;
                            break;
                        default:
                            state.ui_screen = MENU_BACKUP_RESTORE;
                    }
                    // Reset menu selection
                    ui_state.menu_selected = 0;
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
            case MENU_BACKUP:
            case MENU_RESTORE:
                if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
            // Info menu screen
            case MENU_INFO:
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(info_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(info_num);
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
            // About screen, scroll without rollover
            case MENU_ABOUT:
                if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                {
                    if(ui_state.menu_selected > 0)
                        ui_state.menu_selected -= 1;
                }
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    ui_state.menu_selected += 1;
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_TOP);
                break;
#ifdef CONFIG_RTC
            // Time&Date settings screen
            case SETTINGS_TIMEDATE:
                if(msg.keys & KEY_ENTER)
                {
                    // Switch to set Time&Date mode
                    state.ui_screen = SETTINGS_TIMEDATE_SET;
                    // Reset input position and selection
                    ui_state.input_position = 0;
                    memset(&ui_state.new_timedate, 0, sizeof(datetime_t));
                    vp_announceBuffer(&currentLanguage->timeAndDate,
                                      true, false, "dd/mm/yy");
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_SETTINGS);
                break;
            // Time&Date settings screen, edit mode
            case SETTINGS_TIMEDATE_SET:
                if(msg.keys & KEY_ENTER)
                {
                    // Save time only if all digits have been inserted
                    if(ui_state.input_position < TIMEDATE_DIGITS)
                        break;
                    // Return to Time&Date menu, saving values
                    // NOTE: The user inserted a local time, we must save an UTC time
                    datetime_t utc_time = localTimeToUtc(ui_state.new_timedate,
                                                         state.settings.utc_timezone);
                    platform_setTime(utc_time);
                    state.time = utc_time;
                    vp_announceSettingsTimeDate();
                    state.ui_screen = SETTINGS_TIMEDATE;
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(SETTINGS_TIMEDATE);
                else if(input_isNumberPressed(msg))
                {
                    // Discard excess digits
                    if(ui_state.input_position > TIMEDATE_DIGITS)
                        break;
                    ui_state.input_position += 1;
                    ui_state.input_number = input_getPressedNumber(msg);
                    _ui_timedate_add_digit(&ui_state.new_timedate, ui_state.input_position,
                                            ui_state.input_number);
                }
                break;
#endif
            case SETTINGS_DISPLAY:
                if(msg.keys & KEY_LEFT || (ui_state.edit_mode &&
                   (msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT)))
                {
                    switch(ui_state.menu_selected)
                    {
#ifdef CONFIG_SCREEN_BRIGHTNESS
                        case D_BRIGHTNESS:
                            _ui_changeBrightness(-5);
                            vp_announceSettingsInt(&currentLanguage->brightness, queueFlags,
                                                   state.settings.brightness);
                            break;
#endif
#ifdef CONFIG_SCREEN_CONTRAST
                        case D_CONTRAST:
                            _ui_changeContrast(-4);
                            vp_announceSettingsInt(&currentLanguage->brightness, queueFlags,
                                                   state.settings.contrast);
                            break;
#endif
                        case D_TIMER:
                            _ui_changeTimer(-1);
                            vp_announceDisplayTimer();
                            break;
                        case D_BATTERY:
                            state.settings.showBatteryIcon = !state.settings.showBatteryIcon;
                            break;
                        default:
                            state.ui_screen = SETTINGS_DISPLAY;
                    }
                }
                else if(msg.keys & KEY_RIGHT || (ui_state.edit_mode &&
                        (msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)))
                {
                    switch(ui_state.menu_selected)
                    {
#ifdef CONFIG_SCREEN_BRIGHTNESS
                        case D_BRIGHTNESS:
                            _ui_changeBrightness(+5);
                            vp_announceSettingsInt(&currentLanguage->brightness, queueFlags,
                                                   state.settings.brightness);
                            break;
#endif
#ifdef CONFIG_SCREEN_CONTRAST
                        case D_CONTRAST:
                            _ui_changeContrast(+4);
                            vp_announceSettingsInt(&currentLanguage->brightness, queueFlags,
                                                   state.settings.contrast);
                            break;
#endif
                        case D_TIMER:
                            _ui_changeTimer(+1);
                            vp_announceDisplayTimer();
                            break;
                        default:
                            state.ui_screen = SETTINGS_DISPLAY;
                    }
                }
                else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(display_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(display_num);
                else if(msg.keys & KEY_ENTER)
                    ui_state.edit_mode = !ui_state.edit_mode;
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_SETTINGS);
                break;
#ifdef CONFIG_GPS
            case SETTINGS_GPS:
                if(msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT ||
                   (ui_state.edit_mode &&
                   (msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT ||
                    msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)))
                {
                    switch(ui_state.menu_selected)
                    {
                        case G_ENABLED:
                            if(state.settings.gps_enabled)
                                state.settings.gps_enabled = 0;
                            else
                                state.settings.gps_enabled = 1;
                            vp_announceSettingsOnOffToggle(&currentLanguage->gpsEnabled,
                                                           queueFlags,
                                                           state.settings.gps_enabled);
                            break;
#ifdef CONFIG_RTC
                        case G_SET_TIME:
                            state.settings.gpsSetTime = !state.settings.gpsSetTime;
                            vp_announceSettingsOnOffToggle(&currentLanguage->gpsSetTime,
                                                           queueFlags,
                                                           state.settings.gpsSetTime);
                            break;
                        case G_TIMEZONE:
                            if(msg.keys & KEY_LEFT || msg.keys & KEY_DOWN ||
                               msg.keys & KNOB_LEFT)
                                state.settings.utc_timezone -= 1;
                            else if(msg.keys & KEY_RIGHT || msg.keys & KEY_UP ||
                                    msg.keys & KNOB_RIGHT)
                                state.settings.utc_timezone += 1;
                            vp_announceTimeZone(state.settings.utc_timezone, queueFlags);
                            break;
#endif
                        default:
                            state.ui_screen = SETTINGS_GPS;
                    }
                }
                else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_gps_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_gps_num);
                else if(msg.keys & KEY_ENTER)
                    ui_state.edit_mode = !ui_state.edit_mode;
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_SETTINGS);
                break;
#endif
            // Radio Settings
            case SETTINGS_RADIO:
                // If the entry is selected with enter we are in edit_mode
                if (ui_state.edit_mode)
                {
                    switch(ui_state.menu_selected)
                    {
                        case R_OFFSET:
                            // Handle offset frequency input
#if defined(CONFIG_UI_NO_KEYBOARD)
                            if(msg.long_press && msg.keys & KEY_ENTER)
                            {
                                // Long press on CONFIG_UI_NO_KEYBOARD causes digits to advance by one
                                ui_state.new_offset /= 10;
#else
                            if(msg.keys & KEY_ENTER)
                            {
#endif
                                // Apply new offset if it is within the hardware
                                // limits of the radio
                                freq_t new_freq = state.channel.rx_frequency + ui_state.new_offset;
                                if (_ui_freq_check_limits(new_freq))
                                {
                                    state.channel.tx_frequency = new_freq;
                                    vp_queueStringTableEntry(&currentLanguage->offset);
                                    vp_queueFrequency(ui_state.new_offset);
                                    ui_state.edit_mode = false;
                                }
                            }
                            else
                            if(msg.keys & KEY_ESC)
                            {
                                // Announce old frequency offset
                                vp_queueStringTableEntry(&currentLanguage->offset);
                                vp_queueFrequency((int32_t)state.channel.tx_frequency - (int32_t)state.channel.rx_frequency);
                            }
                            else if(msg.keys & KEY_UP || msg.keys & KEY_DOWN ||
                                    msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT)
                            {
                                _ui_numberInputDel(&ui_state.new_offset);
                            }
#if defined(CONFIG_UI_NO_KEYBOARD)
                            else if(msg.keys & KNOB_LEFT || msg.keys & KNOB_RIGHT || msg.keys & KEY_ENTER)
#else
                            else if(input_isNumberPressed(msg))
#endif
                            {
                                _ui_numberInputKeypad(&ui_state.new_offset, msg);
                                ui_state.input_position += 1;
                            }
                            else if (msg.long_press && (msg.keys & KEY_F1) && (state.settings.vpLevel > vpBeep))
                            {
                                vp_queueFrequency(ui_state.new_offset);
                                f1Handled=true;
                            }
                            break;
                        case R_DIRECTION:
                            if(msg.keys & KEY_UP || msg.keys & KEY_DOWN ||
                               msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT ||
                               msg.keys & KNOB_LEFT || msg.keys & KNOB_RIGHT)
                            {
                                // Invert frequency offset direction
                                if (state.channel.tx_frequency >= state.channel.rx_frequency)
                                    state.channel.tx_frequency -= 2 * ((int32_t)state.channel.tx_frequency - (int32_t)state.channel.rx_frequency);
                                else // Switch to positive offset
                                    state.channel.tx_frequency -= 2 * ((int32_t)state.channel.tx_frequency - (int32_t)state.channel.rx_frequency);
                            }
                            break;
                        case R_STEP:
                            if (msg.keys & KEY_UP || msg.keys & KEY_RIGHT || msg.keys & KNOB_RIGHT)
                            {
                                // Cycle over the available frequency steps
                                state.step_index++;
                                state.step_index %= n_freq_steps;
                            }
                            else if(msg.keys & KEY_DOWN || msg.keys & KEY_LEFT || msg.keys & KNOB_LEFT)
                            {
                                state.step_index += n_freq_steps;
                                state.step_index--;
                                state.step_index %= n_freq_steps;
                            }
                            break;
                        default:
                            state.ui_screen = SETTINGS_RADIO;
                    }
                    // If ENTER or ESC are pressed, exit edit mode, R_OFFSET is managed separately
                    if((ui_state.menu_selected != R_OFFSET && msg.keys & KEY_ENTER) || msg.keys & KEY_ESC)
                        ui_state.edit_mode = false;
                }
                else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_radio_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_radio_num);
                else if(msg.keys & KEY_ENTER) {
                    ui_state.edit_mode = true;
                    // If we are entering R_OFFSET clear temp offset
                    if (ui_state.menu_selected == R_OFFSET)
                        ui_state.new_offset = 0;
                    // Reset input position
                    ui_state.input_position = 0;
                }
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_SETTINGS);
                break;
#ifdef CONFIG_M17
            // M17 Settings
            case SETTINGS_M17:
                if(ui_state.edit_mode)
                {
                    switch (ui_state.menu_selected)
                    {
                        case M17_CALLSIGN:
                            // Handle text input for M17 callsign
                            if(msg.keys & KEY_ENTER)
                            {
                                _ui_textInputConfirm(ui_state.new_callsign);
                                // Save selected callsign and disable input mode
                                strncpy(state.settings.callsign, ui_state.new_callsign, 10);
                                ui_state.edit_mode = false;
                                vp_announceBuffer(&currentLanguage->callsign,
                                                  false, true, state.settings.callsign);
                            }
                            else if(msg.keys & KEY_ESC)
                            {
                                // Discard selected callsign and disable input mode
                                ui_state.edit_mode = false;
                                vp_announceBuffer(&currentLanguage->callsign,
                                                  false, true, state.settings.callsign);
                            }
                            else if(msg.keys & KEY_UP || msg.keys & KEY_DOWN ||
                                     msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT)
                            {
                                _ui_textInputDel(ui_state.new_callsign);
                            }
                            else if(input_isCharPressed(msg))
                            {
                                _ui_textInputKeypad(ui_state.new_callsign, 9, msg, true);
                            }
                            else if (msg.long_press && (msg.keys & KEY_F1) && (state.settings.vpLevel > vpBeep))
                            {
                                vp_announceBuffer(&currentLanguage->callsign,
                                                  true, true, ui_state.new_callsign);
                                f1Handled=true;
                            }
                            break;
                        case M17_METATEXT:
                            // Handle text input for the M17 meta text
                            if(msg.keys & KEY_ENTER)
                            {
                                _ui_textInputConfirm(ui_state.new_meta_text);
                                // Save selected message and disable input mode
                                strncpy(state.settings.M17_meta_text, ui_state.new_meta_text, 52);
                                ui_state.edit_meta_text = false;
                                ui_state.edit_mode = false;
                                vp_announceBuffer(&currentLanguage->metaText,
                                                  false, true, state.settings.M17_meta_text);
                            }
                            else if(msg.keys & KEY_ESC)
                            {
                                // Discard selected message and disable input mode
                                ui_state.edit_meta_text = false;
                                ui_state.edit_mode = false;
                                vp_announceBuffer(&currentLanguage->metaText,
                                                  false, true, state.settings.M17_meta_text);
                            }
                            else if(msg.keys & KEY_UP || msg.keys & KEY_DOWN ||
                                     msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT)
                            {
                                _ui_textInputDel(ui_state.new_meta_text);
                            }
                            else if(input_isCharPressed(msg))
                            {
                                _ui_textInputKeypad(ui_state.new_meta_text, 52, msg, false);
                            }
                            else if (msg.long_press && (msg.keys & KEY_F1) && (state.settings.vpLevel > vpBeep))
                            {
                                vp_announceBuffer(&currentLanguage->metaText,
                                                  true, true, ui_state.new_meta_text);
                                f1Handled=true;
                            }
                            break;
                        case M17_CAN:
                            if(msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT)
                                _ui_changeM17Can(-1);
                            else if(msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)
                                _ui_changeM17Can(+1);
                            else if(msg.keys & KEY_ENTER)
                                ui_state.edit_mode = !ui_state.edit_mode;
                            else if(msg.keys & KEY_ESC)
                                ui_state.edit_mode = false;
                            break;
                        case M17_CAN_RX:
                            if(msg.keys & KEY_LEFT || msg.keys & KEY_RIGHT ||
                                (ui_state.edit_mode &&
                                 (msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT ||
                                  msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)))
                            {
                                state.settings.m17_can_rx =
                                    !state.settings.m17_can_rx;
                            }
                            else if(msg.keys & KEY_ENTER)
                                ui_state.edit_mode = !ui_state.edit_mode;
                            else if(msg.keys & KEY_ESC)
                                ui_state.edit_mode = false;
                    }
                }
                else
                {
                    if(msg.keys & KEY_ENTER)
                    {
                        // Enable edit mode
                        ui_state.edit_mode = true;

                        // If callsign input, reset text input variables
                        if(ui_state.menu_selected == M17_CALLSIGN)
                        {
                            _ui_textInputReset(ui_state.new_callsign,
                                    sizeof(ui_state.new_callsign));
                            vp_announceBuffer(&currentLanguage->callsign,
                                            true, true, ui_state.new_callsign);
                        }
                        // If message input, reset text input variables
                        if(ui_state.menu_selected == M17_METATEXT)
                        {
                            //   ui_state.edit_mode = false;
                            ui_state.edit_meta_text = true;
                            _ui_textInputReset(ui_state.new_meta_text,
                                    sizeof(ui_state.new_meta_text));
                            vp_announceBuffer(&currentLanguage->metaText,
                                            true, true, ui_state.new_meta_text);
                        }
                    }
                    else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                        _ui_menuUp(settings_m17_num);
                    else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                        _ui_menuDown(settings_m17_num);
                    else if((msg.keys & KEY_RIGHT) && (ui_state.menu_selected == M17_CAN))
                            _ui_changeM17Can(+1);
                    else if((msg.keys & KEY_LEFT)  && (ui_state.menu_selected == M17_CAN))
                            _ui_changeM17Can(-1);
                    else if(msg.keys & KEY_ESC)
                    {
                        *sync_rtx = true;
                        _ui_menuBack(MENU_SETTINGS);
                    }
                }
                break;
#endif
#ifdef CONFIG_MEMCH
            // Memory: choose a slot, then save, rename or delete
            case SETTINGS_MEMORY:
                _ui_memoryKeys(msg);
                break;
#endif
            // FM settings: Enter, then Up / Down change the value
            case SETTINGS_FM:
                if(ui_state.edit_mode &&
                   (msg.keys & (KEY_UP | KEY_DOWN | KNOB_LEFT | KNOB_RIGHT |
                                KEY_LEFT | KEY_RIGHT)))
                {
                    int step = (msg.keys & (KEY_UP | KNOB_RIGHT | KEY_RIGHT))
                             ? 1 : -1;
                    _ui_changeFmSetting(ui_state.menu_selected, step);
                    *sync_rtx = true;
                }
                else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_fm_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_fm_num);
                else if((msg.keys & KEY_ENTER) && !ui_state.edit_mode
                        && ((ui_state.menu_selected == FM_TX_TONE)
                            || (ui_state.menu_selected == FM_RX_TONE)))
                    _ui_tonePickOpen(ui_state.menu_selected == FM_TX_TONE);
                else if(msg.keys & KEY_ENTER)
                    ui_state.edit_mode = !ui_state.edit_mode;
                else if(msg.keys & KEY_ESC)
                {
                    if(ui_state.edit_mode)
                        ui_state.edit_mode = false;
                    else
                        _ui_menuBack(MENU_SETTINGS);
                }
                break;

            case TONE_PICK:
                _ui_tonePickKey(msg, sync_rtx);
                break;

            #ifdef CONFIG_BCAST_FM
            case BCAST_FM:
                _ui_bcastKey(msg);
                break;
            #endif

            #ifdef CONFIG_TETRA_CALLREC
            case TETRA_SAVED:
                _ui_tetraSavedKey(msg);
                break;
            #endif

            #ifdef CONFIG_TETRA
            // Messages: up / down select, green opens one, red goes back
            case TETRA_MESSAGES:
            case TETRA_MESSAGE:
            {
                uint8_t count = tetraStatus_messageCount();
                if(msg.long_press || msg.keys == 0)
                    break;
                if(msg.keys & (KEY_UP | KNOB_LEFT))
                {
                    if(ui_state.msg_sel > 0)
                        ui_state.msg_sel--;
                }
                else if(msg.keys & (KEY_DOWN | KNOB_RIGHT))
                {
                    if(ui_state.msg_sel + 1 < count)
                        ui_state.msg_sel++;
                }
                else if((msg.keys & KEY_ENTER) && count > 0
                        && state.ui_screen == TETRA_MESSAGES)
                    state.ui_screen = TETRA_MESSAGE;
                else if(msg.keys & KEY_ESC)
                {
                    if(state.ui_screen == TETRA_MESSAGE)
                        state.ui_screen = TETRA_MESSAGES;
                    else
                        _ui_menuBack(MENU_TOP);
                }
                break;
            }

            // Activity: up / down select, green shows one, red goes back
            case TETRA_ACTIVITY:
            case TETRA_ACTIVITY_DETAIL:
            {
                uint8_t count = tetraStatus_activityCount();
                if(msg.long_press || msg.keys == 0)
                    break;
                if(msg.keys & (KEY_UP | KNOB_LEFT))
                {
                    if(ui_state.activity_sel > 0)
                        ui_state.activity_sel--;
                }
                else if(msg.keys & (KEY_DOWN | KNOB_RIGHT))
                {
                    if(ui_state.activity_sel + 1 < count)
                        ui_state.activity_sel++;
                }
                else if((msg.keys & KEY_ENTER) && count > 0
                        && state.ui_screen == TETRA_ACTIVITY)
                    state.ui_screen = TETRA_ACTIVITY_DETAIL;
                else if(msg.keys & KEY_ESC)
                {
                    if(state.ui_screen == TETRA_ACTIVITY_DETAIL)
                        state.ui_screen = TETRA_ACTIVITY;
                    else
                        _ui_menuBack(MENU_TOP);
                }
                break;
            }
            #endif

            case SETTINGS_ACCESSIBILITY:
                if(msg.keys & KEY_LEFT || (ui_state.edit_mode &&
                   (msg.keys & KEY_DOWN || msg.keys & KNOB_LEFT)))
                {
                    switch(ui_state.menu_selected)
                    {
                        case A_MACRO_LATCH:
                            _ui_changeMacroLatch(false);
                            break;
                        case A_LEVEL:
                            _ui_changeVoiceLevel(-1);
                            break;
                        case A_PHONETIC:
                            _ui_changePhoneticSpell(false);
                            break;
                        default:
                            state.ui_screen = SETTINGS_ACCESSIBILITY;
                    }
                }
                else if(msg.keys & KEY_RIGHT || (ui_state.edit_mode &&
                        (msg.keys & KEY_UP || msg.keys & KNOB_RIGHT)))
                {
                    switch(ui_state.menu_selected)
                    {
                        case A_MACRO_LATCH:
                            _ui_changeMacroLatch(true);
                            break;
                        case A_LEVEL:
                            _ui_changeVoiceLevel(1);
                            break;
                        case A_PHONETIC:
                            _ui_changePhoneticSpell(true);
                            break;
                        default:
                            state.ui_screen = SETTINGS_ACCESSIBILITY;
                    }
                }
                else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_accessibility_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_accessibility_num);
                else if(msg.keys & KEY_ENTER)
                    ui_state.edit_mode = !ui_state.edit_mode;
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_SETTINGS);
                break;
#ifdef CONFIG_TETRA
            // TETRA settings: Enter, then Up / Down switch on and off
            case SETTINGS_TETRA:
                if(ui_state.edit_mode &&
                   (msg.keys & (KEY_UP | KEY_DOWN | KNOB_LEFT | KNOB_RIGHT |
                                KEY_LEFT | KEY_RIGHT)))
                {
                    bool on = (msg.keys & (KEY_UP | KNOB_RIGHT | KEY_RIGHT)) != 0;
                    _ui_changeTetraSetting(ui_state.menu_selected, on);
                }
                else if(msg.keys & KEY_UP || msg.keys & KNOB_LEFT)
                    _ui_menuUp(settings_tetra_num);
                else if(msg.keys & KEY_DOWN || msg.keys & KNOB_RIGHT)
                    _ui_menuDown(settings_tetra_num);
                else if(msg.keys & KEY_ENTER)
                    ui_state.edit_mode = !ui_state.edit_mode;
                else if(msg.keys & KEY_ESC)
                    _ui_menuBack(MENU_SETTINGS);
                break;
#endif
            case SETTINGS_RESET2DEFAULTS:
                if(! ui_state.edit_mode){
                    //require a confirmation ENTER, then another
                    //edit_mode is slightly misused to allow for this
                    if(msg.keys & KEY_ENTER)
                    {
                        ui_state.edit_mode = true;
                    }
                    else if(msg.keys & KEY_ESC)
                    {
                        _ui_menuBack(MENU_SETTINGS);
                    }
                } else {
                    if(msg.keys & KEY_ENTER)
                    {
                        ui_state.edit_mode = false;
                        state_resetSettingsAndVfo();
#ifdef CONFIG_TETRA
                        _ui_tetraApplySettings();
#endif
                        _ui_menuBack(MENU_SETTINGS);
                    }
                    else if(msg.keys & KEY_ESC)
                    {
                        ui_state.edit_mode = false;
                        _ui_menuBack(MENU_SETTINGS);
                    }
                }
                break;
        }

        // Enable Tx only if in MAIN_VFO or MAIN_MEM states
        bool inMemOrVfo = (state.ui_screen == MAIN_VFO) || (state.ui_screen == MAIN_MEM);
        #ifdef CONFIG_BCAST_FM
        // The FM channel stays in use behind the broadcast radio
        inMemOrVfo = inMemOrVfo || (state.ui_screen == BCAST_FM);
        #endif
        if ((macro_menu == true) || ((inMemOrVfo == false) && (state.txDisable == false)))
        {
            state.txDisable = true;
            *sync_rtx = true;
        }
        if (!f1Handled && (msg.keys & KEY_F1) && (state.settings.vpLevel > vpBeep))
        {
            vp_replayLastPrompt();
        }
        else if ((priorUIScreen!=state.ui_screen) && state.settings.vpLevel > vpLow)
        {
            // When we switch to VFO or Channel screen, we need to announce it.
            // Likewise for information screens.
            // All other cases are handled as needed.
            vp_announceScreen(state.ui_screen);
        }
        // generic beep for any keydown if beep is enabled.
        // At vp levels higher than beep, keys will generate voice so no need
        // to beep or you'll get an unwanted click.
        if ((msg.keys &0xffff) && (state.settings.vpLevel == vpBeep))
            vp_beep(BEEP_KEY_GENERIC, SHORT_BEEP);
        // If we exit and re-enter the same menu, we want to ensure it speaks.
        if (msg.keys & KEY_ESC)
            _ui_reset_menu_anouncement_tracking();
    }
    else if(event.type == EVENT_STATUS)
    {
#ifdef CONFIG_SCAN
        _ui_scanTick(now, sync_rtx);
#endif
        // Channel number typed and left: jump to it
        if(ui_state.ch_entry_digits > 0)
        {
            if(state.ui_screen != MAIN_MEM)
                _ui_chEntryCancel();
            else if(now - ui_state.ch_entry_tick >= CH_ENTRY_TIMEOUT_MS)
                _ui_chEntryConfirm(sync_rtx);
        }

#ifdef CONFIG_GPS
        if ((state.ui_screen == MENU_GPS) &&
            (!vp_isPlaying()) &&
            (state.settings.vpLevel > vpLow) &&
            (!txOngoing && !rtx_rxSquelchOpen()))
        {// automatically read speed and direction changes only!
            enum vpGPSInfoFlags whatChanged = GetGPSDirectionOrSpeedChanged();
            if (whatChanged != vpGPSNone)
                vp_announceGPSInfo(whatChanged);
        }
#endif //            CONFIG_GPS

        bool tetraCall = false;
        #ifdef CONFIG_TETRA
        // A TETRA call wakes the screen and keeps it on
        tetraCall = _ui_tetraCallActive();
        #endif

        bool watching = false;
        #ifdef CONFIG_SPECTRUM
        // The spectrum view keeps the screen on
        watching = (state.ui_screen == SPECTRUM);
        #endif

        // VOX transmits without PTT
        if (rtx_getCurrentStatus().opStatus == TX)
            watching = true;

        if (txOngoing || rtx_rxSquelchOpen() || tetraCall || watching ||
            (state.volume != last_state.volume))
        {
            _ui_exitStandby(now);
            return;
        }

        if (_ui_checkStandby(now - last_event_tick))
        {
            _ui_enterStandby();
        }
    }
}

bool ui_updateGUI()
{
    if(redraw_needed == false)
        return false;

    if(!layout_ready)
    {
        _ui_calculateLayout(&layout);
        layout_ready = true;
    }
    // Draw current GUI page
    switch(last_state.ui_screen)
    {
        // VFO main screen
        case MAIN_VFO:
            _ui_drawMainVFO(&ui_state);
            break;
        // VFO frequency input screen
        case MAIN_VFO_INPUT:
            _ui_drawMainVFOInput(&ui_state);
            break;
        // MEM main screen
        case MAIN_MEM:
            _ui_drawMainMEM(&ui_state);
            break;
        // Top menu screen
        case MENU_TOP:
            _ui_drawMenuTop(&ui_state);
            break;
        // Zone menu screen
        case MENU_BANK:
            _ui_drawMenuBank(&ui_state);
            break;
        // Channel menu screen
        case MENU_CHANNEL:
            _ui_drawMenuChannel(&ui_state);
            break;
        // Contacts menu screen
        case MENU_CONTACTS:
            _ui_drawMenuContacts(&ui_state);
            break;
#ifdef CONFIG_GPS
        // GPS menu screen
        case MENU_GPS:
            _ui_drawMenuGPS();
            break;
#endif
        // Settings menu screen
        case MENU_SETTINGS:
            _ui_drawMenuSettings(&ui_state);
            break;
#ifdef CONFIG_TETRA
        // TETRA recent calls
        case TETRA_RECENT:
            _ui_drawTetraRecent(&ui_state);
            break;
        case TETRA_RECENT_DETAIL:
            _ui_drawTetraRecentDetail(&ui_state);
            break;
        case TETRA_FIELD_TEST:
            _ui_drawTetraFieldTest(&ui_state);
            break;
#endif
#ifdef CONFIG_FAST_COPY
        case FAST_COPY:
            _ui_drawFastCopy(&ui_state);
            break;
#endif
#ifdef CONFIG_SPECTRUM
        case SPECTRUM:
            _ui_drawSpectrum(&ui_state);
            break;
#endif
        // Flash backup and restore screen
        case MENU_BACKUP_RESTORE:
            _ui_drawMenuBackupRestore(&ui_state);
            break;
        // Flash backup screen
        case MENU_BACKUP:
            _ui_drawMenuBackup(&ui_state);
            break;
        // Flash restore screen
        case MENU_RESTORE:
            _ui_drawMenuRestore(&ui_state);
            break;
        // Info menu screen
        case MENU_INFO:
            _ui_drawMenuInfo(&ui_state);
            break;
        // About menu screen
        case MENU_ABOUT:
            _ui_drawMenuAbout(&ui_state);
            break;
#ifdef CONFIG_RTC
        // Time&Date settings screen
        case SETTINGS_TIMEDATE:
            _ui_drawSettingsTimeDate();
            break;
        // Time&Date settings screen, edit mode
        case SETTINGS_TIMEDATE_SET:
            _ui_drawSettingsTimeDateSet(&ui_state);
            break;
#endif
        // Display settings screen
        case SETTINGS_DISPLAY:
            _ui_drawSettingsDisplay(&ui_state);
            break;
#ifdef CONFIG_GPS
        // GPS settings screen
        case SETTINGS_GPS:
            _ui_drawSettingsGPS(&ui_state);
            break;
#endif
#ifdef CONFIG_M17
        // M17 settings screen
        case SETTINGS_M17:
            _ui_drawSettingsM17(&ui_state);
            break;
#endif
        // FM settings screen
        case SETTINGS_FM:
            _ui_drawSettingsFM(&ui_state);
            break;
        case TONE_PICK:
            _ui_drawTonePick(&ui_state);
            break;
#ifdef CONFIG_BCAST_FM
        case BCAST_FM:
            _ui_drawBcast(&ui_state);
            break;
#endif
#ifdef CONFIG_TETRA_CALLREC
        case TETRA_SAVED:
            _ui_drawTetraSaved(&ui_state);
            break;
#endif
#ifdef CONFIG_TETRA
        case TETRA_MESSAGES:
            _ui_drawTetraMessages(&ui_state);
            break;
        case TETRA_MESSAGE:
            _ui_drawTetraMessage(&ui_state);
            break;
        case TETRA_ACTIVITY:
            _ui_drawTetraActivity(&ui_state);
            break;
        case TETRA_ACTIVITY_DETAIL:
            _ui_drawTetraActivityDetail(&ui_state);
            break;
#endif
#ifdef CONFIG_SELFTEST
        case SELF_TEST:
            _ui_drawSelfTest(&ui_state);
            break;
#endif
#ifdef CONFIG_MEMCH
        case SETTINGS_MEMORY:
            _ui_drawSettingsMemory(&ui_state);
            break;
#endif
#ifdef CONFIG_TETRA
        // TETRA settings screen
        case SETTINGS_TETRA:
            _ui_drawSettingsTetra(&ui_state);
            break;
#endif
        case SETTINGS_ACCESSIBILITY:
            _ui_drawSettingsAccessibility(&ui_state);
            break;
        // Screen to support resetting Settings and VFO to defaults
        case SETTINGS_RESET2DEFAULTS:
            _ui_drawSettingsReset2Defaults(&ui_state);
            break;
        // Screen to set frequency offset and step
        case SETTINGS_RADIO:
            _ui_drawSettingsRadio(&ui_state);
            break;
        // Low battery screen
        case LOW_BAT:
            _ui_drawLowBatteryScreen();
            break;
    }

#ifdef CONFIG_REPLAY
    // Instant replay playing: a banner over the top of the main screens
    bool replayHere = (state.ui_screen == MAIN_VFO)
                   || (state.ui_screen == MAIN_MEM);
    #ifdef CONFIG_BCAST_FM
    replayHere = replayHere || (state.ui_screen == BCAST_FM);
    #endif
    if(replayHere)
        _ui_drawReplay();
#endif

    // If MACRO menu is active draw it
    if(macro_menu)
    {
        _ui_drawDarkOverlay();
        _ui_drawMacroMenu(&ui_state);
    }

    redraw_needed = false;
    return true;
}

bool ui_pushEvent(const uint8_t type, const uint32_t data)
{
    uint8_t newHead = (evQueue_wrPos + 1) % MAX_NUM_EVENTS;

    // Queue is full
    if(newHead == evQueue_rdPos) return false;

    // Preserve atomicity when writing the new element into the queue.
    event_t event;
    event.type    = type;
    event.payload = data;

    evQueue[evQueue_wrPos] = event;
    evQueue_wrPos = newHead;

    return true;
}

void ui_terminate()
{
}
