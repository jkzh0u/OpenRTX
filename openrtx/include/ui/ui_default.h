/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef UI_DEFAULT_H
#define UI_DEFAULT_H

#include <stdbool.h>
#include "core/state.h"
#include "core/graphics.h"
#include "interfaces/keyboard.h"
#include <stdint.h>
#include "core/event.h"
#include "hwconfig.h"
#include "core/ui.h"
#ifdef CONFIG_SELFTEST
#include "interfaces/selftest.h"

/* Self test results kept for the parts page */
#define SELFTEST_ITEMS 16
#endif

// Maximum menu entry length
#define MAX_ENTRY_LEN 21
// Frequency digits
#define FREQ_DIGITS 7
// Time & Date digits
#define TIMEDATE_DIGITS 10
// Max number of UI events
#define MAX_NUM_EVENTS 16

enum uiScreen
{
    MAIN_VFO = 0,
    MAIN_VFO_INPUT,
    MAIN_MEM,
    MODE_VFO,
    MODE_MEM,
    MENU_TOP,
    MENU_BANK,
    MENU_CHANNEL,
    MENU_CONTACTS,
    MENU_GPS,
    MENU_SETTINGS,
    MENU_BACKUP_RESTORE,
    MENU_BACKUP,
    MENU_RESTORE,
    MENU_INFO,
    MENU_ABOUT,
    SETTINGS_TIMEDATE,
    SETTINGS_TIMEDATE_SET,
    SETTINGS_DISPLAY,
    SETTINGS_GPS,
    SETTINGS_RADIO,
    SETTINGS_M17,
    SETTINGS_FM,
    SETTINGS_ACCESSIBILITY,
    SETTINGS_RESET2DEFAULTS,
    LOW_BAT,
    TETRA_RECENT,
    TETRA_RECENT_DETAIL,
    TETRA_FIELD_TEST,
    SETTINGS_TETRA,
    FAST_COPY,
    SETTINGS_MEMORY,
    SPECTRUM,
    TONE_PICK,
    BCAST_FM,
    TETRA_SAVED,
    TETRA_MESSAGES,
    TETRA_MESSAGE,
    TETRA_ACTIVITY,
    TETRA_ACTIVITY_DETAIL,
    SELF_TEST
};

/* FM scan: what it does after stopping on a signal (settings.scanResume) */
enum scanResume
{
    SCAN_RESUME_CARRIER = 0, // Goes on once the signal has gone a while
    SCAN_RESUME_TIME,        // Goes on after a few seconds, signal or not
    SCAN_RESUME_STOP,        // Ends the scan there
    SCAN_RESUME_NUM
};

enum SetRxTx
{
    SET_RX = 0,
    SET_TX
};

// This enum is needed to have item numbers that match
// menu elements even if some elements may be missing (GPS)
enum menuItems
{
    M_BANK = 0,
    M_CHANNEL,
    M_CONTACTS,
#ifdef CONFIG_GPS
    M_GPS,
#endif
#ifdef CONFIG_BCAST_FM
    M_BCAST,
#endif
#ifdef CONFIG_TETRA_CALLREC
    M_SAVED_CALLS,
#endif
#ifdef CONFIG_TETRA
    M_MESSAGES,
    M_ACTIVITY,
#endif
    M_SETTINGS,
    M_INFO,
#ifdef CONFIG_SELFTEST
    M_SELF_TEST,
#endif
#ifdef CONFIG_TETRA
    M_FIELD_TEST,
#endif
    M_ABOUT
};

enum settingsItems
{
    S_DISPLAY = 0,
#ifdef CONFIG_RTC
    S_TIMEDATE,
#endif
#ifdef CONFIG_GPS
    S_GPS,
#endif
    S_RADIO,
#ifdef CONFIG_M17
    S_M17,
#endif
    S_FM,
#ifdef CONFIG_MEMCH
    S_MEMORY,
#endif
#ifdef CONFIG_TETRA
    S_TETRA,
#endif
    S_ACCESSIBILITY,
    S_RESET2DEFAULTS,
};

enum backupRestoreItems
{
    BR_BACKUP = 0,
    BR_RESTORE
};

enum displayItems
{
#ifdef CONFIG_SCREEN_BRIGHTNESS
    D_BRIGHTNESS = 0,
#endif
#ifdef CONFIG_SCREEN_CONTRAST
    D_CONTRAST,
#endif
    D_TIMER,
    D_BATTERY
};

#ifdef CONFIG_GPS
enum settingsGPSItems
{
    G_ENABLED = 0,
#ifdef CONFIG_RTC
    G_SET_TIME,
    G_TIMEZONE
#endif
};
#endif

enum settingsAccessibilityItems
{
    A_MACRO_LATCH = 0,
    A_LEVEL,
    A_PHONETIC,
};

#ifdef CONFIG_TETRA
enum settingsTetraItems
{
    T_FOLLOW = 0,
    T_JOIN,
    T_ROAM,
    T_ALARM_BEEP,
    T_KEEP_FILTER,
#ifdef CONFIG_TETRA_CALLREC
    T_SAVE_CALLS,
#endif
#ifdef CONFIG_TETRA_AFC
    T_AFC,
#endif
};
#endif

enum settingsRadioItems
{
    R_OFFSET,
    R_DIRECTION,
    R_STEP,
};

enum settingsM17Items
{
    M17_CALLSIGN = 0,
    M17_METATEXT,
    M17_CAN,
    M17_CAN_RX
};

#ifdef CONFIG_MEMCH
enum settingsMemoryItems
{
    MEM_SLOT = 0,
    MEM_SAVE,
    MEM_RENAME,
    MEM_DELETE
};
#endif

enum settingsFMItems
{
    FM_SQUELCH = 0,
    FM_WIDTH,
    FM_POWER,
    FM_TONE_MODE,
    FM_TX_TONE,
    FM_RX_TONE,
#ifdef CONFIG_ROGER_BEEP
    FM_TALK_PERMIT,
    FM_ROGER_BEEP,
#endif
#ifdef CONFIG_SCAN
    FM_SCAN_RESUME,
#endif
#ifdef CONFIG_VOX
    FM_VOX,
    FM_VOX_DELAY,
#endif
#ifdef CONFIG_NOISE_REDUCTION
    FM_NR_RX,
    FM_NR_TX,
#endif
#ifdef CONFIG_DTMF
    FM_TX_HASH,
#endif
};

/**
 * Struct containing a set of positions and sizes that get
 * calculated for the selected display size.
 * Using these parameters make the UI automatically adapt
 * To displays of different sizes
 */
typedef struct layout_t
{
    uint16_t hline_h;
    uint16_t top_h;
    uint16_t line1_h;
    uint16_t line2_h;
    uint16_t line3_h;
    uint16_t line3_large_h;
    uint16_t line4_h;
    uint16_t menu_h;
    uint16_t bottom_h;
    uint16_t bottom_pad;
    uint16_t status_v_pad;
    uint16_t horizontal_pad;
    uint16_t text_v_offset;
    point_t top_pos;
    point_t line1_pos;
    point_t line2_pos;
    point_t line3_pos;
    point_t line3_large_pos;
    point_t line4_pos;
    point_t bottom_pos;
    fontSize_t top_font;
    symbolSize_t top_symbol_size;
    fontSize_t line1_font;
    symbolSize_t line1_symbol_size;
    fontSize_t line2_font;
    symbolSize_t line2_symbol_size;
    fontSize_t line3_font;
    symbolSize_t line3_symbol_size;
    fontSize_t line3_large_font;
    fontSize_t line4_font;
    symbolSize_t line4_symbol_size;
    fontSize_t bottom_font;
    fontSize_t input_font;
    fontSize_t menu_font;
    fontSize_t message_font;
} layout_t;

/**
 * This structs contains state variables internal to the
 * UI that need to be kept between executions of the UI
 * This state does not need to be saved on device poweroff
 */
typedef struct ui_state_t
{
    // Index of the currently selected menu entry
    uint8_t menu_selected;
    // If true we can change a menu entry value with UP/DOWN
    bool edit_mode;
    bool input_locked;
    // Variables used for VFO input
    uint8_t input_number;
    uint8_t input_position;
    uint8_t input_set;
    long long last_keypress;
    freq_t new_rx_frequency;
    freq_t new_tx_frequency;
    char new_rx_freq_buf[14];
    char new_tx_freq_buf[14];
    size_t m17_meta_text_scroll_position;
    long long m17_meta_text_last_scroll_tick;
    char new_meta_text[53];
    bool edit_meta_text;
#ifdef CONFIG_RTC
    // Variables used for Time & Date input
    datetime_t new_timedate;
    char new_date_buf[9];
    char new_time_buf[9];
#endif
    char new_callsign[10];
    freq_t new_offset;
    // Memory channel number being typed, 0 digits when none
    uint16_t ch_entry;
    uint8_t ch_entry_digits;
    long long ch_entry_tick;
    // Which state to return to when we exit menu
    uint8_t last_main_state;
#ifdef CONFIG_MEMCH
    // Settings > Memory: the slot chosen, a name being typed, and what the
    // last action did
    uint8_t mem_slot;
    char new_name[16];
    bool mem_naming;
    bool mem_delete_armed;
    const char *mem_status;
#endif
    // Tone picker: TX or RX tone, the kind selected (TP_KIND_...), whether
    // its list is open, the choice selected in it (_ui_tonePick...()),
    // digits typed to jump and when the last one was
    bool tone_pick_tx;
    uint8_t tone_pick_kind;
    bool tone_pick_list;
    uint16_t tone_pick_pos;
    char tone_pick_digits[4];
    long long tone_pick_tick;
#ifdef CONFIG_TETRA
    // Messages: the one selected or shown, 0 the newest
    uint8_t msg_sel;
    // Activity: the talkgroup selected or shown, 0 the busiest
    uint8_t activity_sel;
#endif
#ifdef CONFIG_SELFTEST
    // Self test: the page (SELFTEST_PAGE_...), the first line shown, the
    // results of the checks, and on the keys page the keys seen so far,
    // the last ones pressed and whether long
    uint8_t selftest_page;
    uint16_t selftest_scroll;
    uint8_t selftest_count;
    struct selftestItem selftest_items[SELFTEST_ITEMS];
    uint32_t selftest_keys_seen;
    uint32_t selftest_keys_last;
    bool selftest_keys_long;
#endif
#ifdef CONFIG_TETRA_CALLREC
    // Saved calls: the one selected, 0 the newest; a delete waiting for a
    // second long press
    uint16_t saved_sel;
    bool saved_delete_armed;
#endif
#ifdef CONFIG_SCAN
    // FM scan: running, its direction (+1 or -1), listening or stopped on a
    // signal, when it last hopped or stopped, when the squelch was last
    // open, checks in a row with the squelch open, and memory channels
    bool scan_on;
    int8_t scan_dir;
    bool scan_stopped;
    long long scan_tick;
    long long scan_heard;
    uint8_t scan_opens;
    uint16_t scan_count;
#endif
#ifdef CONFIG_BCAST_FM
    // Broadcast FM screen: digits of a frequency being typed, a number or
    // arrow key waiting for its release (its long press does more), and a
    // short message ("saved P3") with when it was shown
    char bcast_digits[5];
    uint32_t bcast_pending;
    const char *bcast_note;
    long long bcast_note_tick;
#endif
#ifdef CONFIG_SPECTRUM
    // Spectrum view: the frequency in the middle and the zoom
    uint32_t spec_center;
    uint8_t spec_zoom;
#endif
#if defined(CONFIG_UI_NO_KEYBOARD)
    uint8_t macro_menu_selected;
#endif // UI_NO_KEYBOARD
}
ui_state_t;

#ifdef CONFIG_SPECTRUM
/* Spectrum view zooms (ui_modes.c): the first is the narrowest */
#define SPECTRUM_ZOOMS 8
#define SPECTRUM_ZOOM_START 3

struct spectrumSweep;

/** @return the distance between the points at a zoom, Hz. */
uint32_t _ui_spectrumStep(uint8_t zoom);

/** @return the strongest point of a sweep, or -1 before the first. */
int _ui_spectrumPeak(const struct spectrumSweep *s);
#endif

#ifdef CONFIG_SELFTEST
/* Self test pages */
enum {
    SELFTEST_PAGE_PARTS = 0, /**< Each part answers or not */
    SELFTEST_PAGE_LIVE,      /**< Signal, battery, audio levels, now */
    SELFTEST_PAGE_KEYS,      /**< Each key, lit once pressed */
    SELFTEST_PAGE_REGS,      /**< Radio chip registers */
    SELFTEST_PAGES
};

/** Draw the self test (ui_menu.c). */
void _ui_drawSelfTest(ui_state_t *ui_state);
#endif

extern layout_t layout;
extern state_t last_state;
extern bool    macro_latched;
extern const char *menu_items[];
extern const char *settings_items[];
extern const char *display_items[];
extern const char *settings_gps_items[];
extern const char *settings_radio_items[];
extern const char *settings_m17_items[];
extern const char *settings_fm_items[];
#ifdef CONFIG_MEMCH
extern const char *settings_memory_items[];
extern const uint8_t settings_memory_num;
#endif
extern const char * settings_accessibility_items[];
#ifdef CONFIG_TETRA
extern const char *settings_tetra_items[];
extern const uint8_t settings_tetra_num;
#endif
extern const char *backup_restore_items[];

/**
 * A channel's FM tone as text: a CTCSS tone, e.g. 88.5, or a DCS code,
 * e.g. D023N (CONFIG_DCS).
 */
void _ui_toneName(char *buf, size_t len, uint8_t index, bool dcs, bool inv);

/**
 * Step an FM tone through the CTCSS tones, then (CONFIG_DCS) the DCS codes,
 * normal then inverted.
 */
void _ui_stepTone(uint8_t *index, bool *dcs, bool *inv, int step);

/*
 * Tone picker: the kind first, then its tones or codes. Every choice has a
 * position: Off, the CTCSS tones, then the DCS codes normal and inverted.
 */

enum {
    TP_KIND_OFF = 0,
    TP_KIND_CTCSS,
    TP_KIND_DCS,     //< CONFIG_DCS
    TP_KIND_DCS_INV, //< CONFIG_DCS
};

/** @return the number of choices. */
uint16_t _ui_tonePickCount(void);

/** @return the number of kinds: Off, CTCSS, with CONFIG_DCS DCS normal and
 * inverted. */
uint8_t _ui_tonePickKinds(void);

/** @return the first position of a kind. */
uint16_t _ui_tonePickStart(uint8_t kind);

/** @return the position after the last of a kind. */
uint16_t _ui_tonePickEnd(uint8_t kind);

/** @return the kind of a position. */
uint8_t _ui_tonePickKind(uint16_t pos);

/** @return the position of a channel's tone setting. */
uint16_t _ui_tonePickPos(bool enabled, uint8_t index, bool dcs, bool inv);

/** The tone setting at a position. */
void _ui_tonePickGet(uint16_t pos, bool *enabled, uint8_t *index, bool *dcs,
                     bool *inv);

/**
 * @param kind: the kind searched.
 * @param digits: digits typed: the whole Hz of a CTCSS tone, or the octal
 * digits of a DCS code.
 * @return the first position of the kind starting with them, or -1.
 */
int _ui_tonePickFind(uint8_t kind, const char *digits);

/** Draw the picker. */
void _ui_drawTonePick(ui_state_t *ui_state);
extern const char *info_items[];
extern const char *authors[];
extern const uint8_t menu_num;
extern const uint8_t settings_num;
extern const uint8_t display_num;
extern const uint8_t settings_gps_num;
extern const uint8_t settings_radio_num;
extern const uint8_t settings_m17_num;
extern const uint8_t settings_fm_num;
extern const uint8_t settings_accessibility_num;
extern const uint8_t backup_restore_num;
extern const uint8_t info_num;
extern const uint8_t author_num;
extern const color_t color_black;
extern const color_t color_grey;
extern const color_t color_white;
extern const color_t yellow_fab413;

#endif /* UI_DEFAULT_H */
