/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#define TARGET_C62

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "drivers/baseband/BK1080.h"
#include "drivers/baseband/BK4819.h"

/*
 * Display properties are encoded in the devicetree
 */
#define DISPLAY DT_CHOSEN(zephyr_display)
#define CONFIG_SCREEN_WIDTH DT_PROP(DISPLAY, width)
#define CONFIG_SCREEN_HEIGHT DT_PROP(DISPLAY, height)

#define CONFIG_PIX_FMT_RGB565

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS
/* Shortcut menu drawn as a grid shaped like the keypad */
#define CONFIG_UI_MACRO_GRID
/* FM main screen drawn like the TETRA one (ui_modes.c) */
#define CONFIG_UI_FM_SCREEN
/* FM roger beep, from the BK4819's tone generator */
#define CONFIG_ROGER_BEEP
/* FM DCS codes as well as CTCSS tones */
#define CONFIG_DCS
/* FM fast copy: read a nearby radio's frequency and tone (long *) */
#define CONFIG_FAST_COPY
/* FM spectrum view (side key + 0) */
#define CONFIG_SPECTRUM
/* FM scan of the memory channels or the VFO (long 0) */
#define CONFIG_SCAN
/* FM VOX: transmit while talking, from the microphone level */
#define CONFIG_VOX
/* FM AI noise reduction, RNNoise on the CSK6 (lib/rnnoise) */
#define CONFIG_NOISE_REDUCTION
/* Broadcast FM radio, 87.5-108 MHz, on the BK1080 (Menu > FM radio) */
#define CONFIG_BCAST_FM
/* Instant replay of what was received, FM and TETRA (P2), 30 s in PSRAM */
#define CONFIG_REPLAY
#define CONFIG_REPLAY_SECTION ".psram_section"
/* DTMF from the keypad while transmitting in FM */
#define CONFIG_DTMF
/* Hardware self test (Menu > Self test, selftest_c62.c) */
#define CONFIG_SELFTEST
/* TETRA calls saved on the radio (Menu > Saved calls, CONFIG_TETRA_CALLREC
 * set by the build with the speech decoder): recorded in PSRAM */
#define CONFIG_TETRA_CALLREC_SECTION ".psram_section"

/* Battery type */
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2

#define CONFIG_M17

/*
 * Settings and VFO are saved a few seconds after they change: at power off
 * the supply collapses too fast for a reliable flash write.
 */
#define CONFIG_NVM_SAVE_ON_CHANGE

/*
 * TETRA automatic frequency correction, by fine tuning of the BK4819: the
 * phase error is lowest with the carrier off the nominal frequency (the
 * filters of the flat receive path are not centred), and the crystal error
 * adds to that.
 */
#define CONFIG_TETRA_AFC

/*
 * TETRA mode: starting taps of the demodulator's equalizer, measured on the
 * BK4819 flat RX path (REG_43 0x7e28). They undo the filters' inter-symbol
 * interference; the equalizer keeps adapting from there.
 */
/* clang-format 18 and 21 disagree on this initializer */
/* clang-format off */
#define CONFIG_TETRA_EQ_INIT \
    { 0.007f, -0.004f, 0.036f, -0.185f, 1.149f, -0.187f, 0.033f, -0.001f, 0.0f }
/* clang-format on */

/* Microphone audio input, set for C62_ADC_MIC_GAIN_D below */
#define CONFIG_MIC_GAIN 13

/*
 * CSK6 codec gains in dB, analog (_A) and digital (_D). The TX DAC sets the
 * level into the BK4819 mic input for both FM and M17; never above 0 dB.
 */
#define C62_ADC_MIC_GAIN_A 6
#define C62_ADC_MIC_GAIN_D 10
#define C62_ADC_RX_GAIN_A 6
#define C62_ADC_RX_GAIN_D 20
#define C62_DAC_SPK_GAIN_A -6
#define C62_DAC_SPK_GAIN_D 0
#define C62_DAC_TX_GAIN_A -18
#define C62_DAC_TX_GAIN_D 0

/**
 * BK4819 and BK1080 device instances. Pin numbers must be kept in sync
 * with the board devicetree (gpioa/gpiob nodes).
 */
extern const struct BK4819 c62_bk4819;

/**
 * Set the fine RX tuning offset, added to the channel frequency (Hz, 10 Hz
 * resolution). Retunes at once when receiving.
 */
void c62_setRxOffset(int32_t hz);

/**
 * @return the fine RX tuning offset, Hz.
 */
int32_t c62_getRxOffset(void);
extern const struct gpioDev c62_bk4819_gpio;
extern const struct BK1080 c62_bk1080;

extern const struct gpio_dt_spec speaker_enable;
extern const struct gpio_dt_spec dtmf_enable;
extern const struct gpio_dt_spec button_ptt;
extern const struct gpio_dt_spec led_white;
extern const struct gpio_dt_spec led_green;
extern const struct gpio_dt_spec led_keyboard;
extern const struct pwm_dt_spec pwm_lcd_backlight;
extern const struct pwm_dt_spec pwm_rf_apc;
extern const struct device *adc_dev;

#ifdef __cplusplus
}
#endif

#endif /* HWCONFIG_H */
