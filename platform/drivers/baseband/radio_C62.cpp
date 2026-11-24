/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/nvmem.h>

#include <hwconfig.h>
#include <interfaces/nvmem.h>
#include <interfaces/radio.h>
#include <interfaces/delays.h>
#include <core/utils.h>

#include <algorithm>
#include <string>

#include "drivers/baseband/BK4819.h"
#include "radioUtils.h"
#ifdef C62_BASEBAND_DUMP
#include "../audio/baseband_dump_c62.h"
#endif

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

/**
 * Set TX power using PWM on pin A03
 * PWM Period: 1000us (1kHz), adjustable duty cycle for power level
 *
 * @param power_percent: 0-100 (0 = min power, 100 = max power)
 */
static void set_tx_power(uint8_t power_percent)
{
    //TODO: Check if this works... May need to adjust mapping of
    // power_percent to duty cycle based on actual power output
    // vs duty cycle curve of the PA, and also consider frequency
    // dependence of PA efficiency

    // Calculate duty cycle (1000us period = 1kHz)
    uint32_t period_us = 1000; // 1kHz PWM frequency
    uint32_t pulse_us = (period_us * power_percent) / 100;

    int ret = pwm_set_dt(&pwm_rf_apc, PWM_USEC(period_us), PWM_USEC(pulse_us));

    if (ret < 0) {
        printk("Failed to set TX power PWM: %d\n", ret);
    }
}

static const rtxStatus_t
    *config; // Pointer to data structure with radio configuration

static enum opstatus radioStatus; // Current operating status
static enum opmode radioMode;     // Current operating mode
static bool rxVhf;                // RX on the VHF LNA, else UHF

/*
 * Fine RX tuning offset, Hz, added to the channel frequency: for tests and
 * a frequency correction loop. See c62_setRxOffset().
 */
static volatile int32_t rxOffset;

/**
 * @return true if the frequency is received through the VHF LNA.
 */
static bool vhfLna(uint32_t freq)
{
    return freq < 174000000;
}

/*
 * BK4819 REG_43 for M17 RX, written whole (see the V3 register list):
 *   <14:12> RF filter bandwidth, 7 = 4.5 kHz (the widest)
 *   <11:9>  RF filter bandwidth used when the signal is weak, same table.
 *           Leaving it narrower than <14:12> makes the chip narrow the
 *           channel filter at low SNR, so set it to the same value.
 *   <8:6>   AF TX LPF2, TX only
 *   <5:4>   BW mode: 0 = 12.5 kHz, 1 = 6.25 kHz, 2 = 25 kHz. <5> doubles
 *           the RF filter bandwidths and the IF (REG_3D).
 *   <3>     undocumented, 1 in every known firmware
 *   <2>     +6 dB after FM demodulation
 *
 * 0x7808: 4.5 kHz / weak 3.75 kHz, 12.5 kHz mode (M17 default)
 * 0x7828: 4.5 kHz / weak 3.75 kHz, 25 kHz mode (doubled)
 * 0x7e28: 4.5 kHz / weak 4.5 kHz,  25 kHz mode (widest documented setting)
 */
#ifdef C62_WIDE_RX
#define C62_M17_RX_REG43 0x7e28
#else
#define C62_M17_RX_REG43 0x7808
#endif

/* Read by the baseband dump: the REG_43 value currently in use for RX */
extern "C" {
volatile uint16_t c62_rx_reg43 = C62_M17_RX_REG43;
}

#ifdef C62_REG43_SWEEP
/*
 * Cycle REG_43 through these values in M17 RX, C62_REG43_SWEEP_MS each, to
 * measure them with a fixed test signal. Each dump packet carries the value
 * in use. The last three have undocumented bits set. The sweep clock only
 * runs while the dump records, so with the default dwell one 10.4 s burst
 * covers all seven values.
 */
static const uint16_t reg43Sweep[] = {
    0x7808, // M17 default, 12.5 kHz
    0x7828, // 25 kHz, weak-signal filter 3.75 kHz
    0x7e28, // 25 kHz, weak-signal filter 4.5 kHz
    0x7e2c, // as above, +6 dB after demodulation
    0x7e38, // BW mode 3 (undocumented)
    0x7e20, // bit 3 cleared (undocumented)
    0xfe28, // bit 15 set (undocumented)
};
#ifndef C62_REG43_SWEEP_MS
#define C62_REG43_SWEEP_MS 1400
#endif
static size_t reg43SweepIndex;
static int64_t reg43SweepLast;    // Uptime at the previous check
static int64_t reg43SweepElapsed; // Recorded time on the current value
static bool reg43SweepStarted;
#endif

/*
 * C62 BK4819 fields for M17, on top of bk4819_setFlatAudio(): RX and TX
 * values, and the bits that keep their FM value in TX. M17 RX uses AF bypass,
 * REG_47 bit 1 and RX gain2 0x3f, see
 * https://wiki.m17foundation.org/index.php?title=A36Plus
 * The FM values are saved when entering M17 and restored when leaving.
 */
static const struct {
    bk4819_reg_t reg;
    uint16_t mask;
    uint16_t rx;
    uint16_t tx;
    uint16_t txFm;
} m17Fields[] = {
    // TX deviation on, raw value 0x582 (not Hz) for the TX DAC gain in
    // hwconfig.h
    { BK4819_REG_40, 0x1fff, 0x1582, 0x1582, 0x0000 },
    // TX: 4.5 kHz RF / 3.75 kHz weak-signal filter, 12.5 kHz channel.
    // RX: overwritten with c62_rx_reg43 by applyM17Fields()
    { BK4819_REG_43, 0x7ffc, 0x7808, 0x7808, 0x0000 },
    { BK4819_REG_47, 0x0002, 0x0002, 0x0000, 0x0002 }, // Bit 1
    // RX gain1 -6 dB, RX gain2 0x3f
    { BK4819_REG_48, 0x0ff0, 0x07f0, 0x0400, 0x03f0 },
    { BK4819_REG_7D, 0x001f, 0x0000, 0x0000, 0x0000 }, // Minimum mic gain
    { BK4819_REG_7E, 0x8000, 0x0000, 0x8000, 0x0000 }, // AGC fix mode in TX
};

static uint16_t fmFields[ARRAY_SIZE(m17Fields)];

/**
 * Digital modes receive through the flat, wide M17 audio path: M17, and
 * TETRA (receive only).
 */
static bool flatMode(enum opmode mode)
{
    return (mode == OPMODE_M17) || (mode == OPMODE_TETRA);
}

/**
 * Apply the M17 audio settings for receiving or transmitting.
 *
 * @param transmit: true for TX, false for RX.
 */
static void applyM17Fields(bool transmit)
{
    bk4819_setFlatAudio(&c62_bk4819, true);

    for (size_t i = 0; i < ARRAY_SIZE(m17Fields); i++) {
        const auto &field = m17Fields[i];
        uint16_t value = BK4819_readReg(&c62_bk4819, field.reg) & ~field.mask;
        value |= transmit ? field.tx | (fmFields[i] & field.txFm) : field.rx;
        BK4819_writeReg(&c62_bk4819, field.reg, value);
    }

    if (!transmit)
        BK4819_writeReg(&c62_bk4819, BK4819_REG_43, c62_rx_reg43);

#ifdef C62_TETRA_IF_DC_FILTER
    /*
     * TETRA receive: re-enable the RX IF DC filter (REG_7E<2:0>, default
     * 0b110) that the flat audio mode bypasses. With it off, the IF DC
     * offset of this low-IF receiver reaches the demodulator as a tone at
     * the IF (16.9 kHz in 25 kHz mode), one-sided: it distorts the +3pi/4
     * symbols much more than the -3pi/4 ones. The wanted signal is centred
     * on the IF, so filtering DC at the IF input does not touch it.
     */
    if (!transmit && radioMode == OPMODE_TETRA) {
        uint16_t value = BK4819_readReg(&c62_bk4819, BK4819_REG_7E);
        BK4819_writeReg(&c62_bk4819, BK4819_REG_7E, (value & ~0x0007) | 0x0006);
    }
#endif
}

/* The BK4819 GPIOs to control the C62 LNA and PA */
enum {
    GPIO_VHF_RX_LNA = 0,
    GPIO_UHF_RX_LNA,
    GPIO_VHF_TX_PA,
    GPIO_UHF_TX_PA,
    GPIO_ALC_TX_LED
};

void radio_init(const rtxStatus_t *rtxState)
{
    config = rtxState;
    radioStatus = OFF;
    radioMode = OPMODE_FM;

    BK4819_SetAF(&c62_bk4819, 0);

    gpioDev_setMode(&c62_bk4819_gpio, GPIO_VHF_RX_LNA, OUTPUT_ACTIVE_LOW);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_UHF_RX_LNA, OUTPUT_ACTIVE_LOW);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_VHF_TX_PA, OUTPUT);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_UHF_TX_PA, OUTPUT);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_ALC_TX_LED, OUTPUT);
}

void radio_terminate()
{
}

void radio_tuneVcxo(const int16_t vhfOffset, const int16_t uhfOffset)
{
    (void)vhfOffset;
    (void)uhfOffset;
}

void radio_setOpmode(const enum opmode mode)
{
    if (mode == radioMode)
        return;

    // The core calls this before disabling the old mode: unkey first
    radio_disableRtx();
    radio_disableAfOutput();

    if (flatMode(radioMode)) {
        for (size_t i = 0; i < ARRAY_SIZE(m17Fields); i++) {
            const auto &field = m17Fields[i];
            uint16_t value = BK4819_readReg(&c62_bk4819, field.reg);
            BK4819_writeReg(&c62_bk4819, field.reg,
                            (value & ~field.mask) | fmFields[i]);
        }
        bk4819_setFlatAudio(&c62_bk4819, false);
    }

    // Save the FM values; radio_enableRx/Tx() apply the M17 ones
    if (flatMode(mode)) {
        for (size_t i = 0; i < ARRAY_SIZE(m17Fields); i++) {
            const auto &field = m17Fields[i];
            fmFields[i] = BK4819_readReg(&c62_bk4819, field.reg) & field.mask;
        }
    }

    radioMode = mode;
}

/**
 * Set the CTCSS/DCS generator and detector for a tone of the rtx
 * configuration: a CTCSS frequency in tenths of Hz, or a DCS code
 * (RTX_TONE_DCS, core/cps.h).
 *
 * @param tone: the tone.
 * @param transmit: for transmitting, else for receiving.
 */
static void setTone(uint16_t tone, bool transmit)
{
#ifdef CONFIG_DCS
    if (tone & RTX_TONE_DCS) {
        uint32_t cw = dcsCodeWord(tone & 0x1ff, (tone & RTX_TONE_DCS_INV) != 0);
        /*
         * CDCSS (mode bit 10 clear), 23-bit code (bit 11 clear), the codeword
         * as given (an inverted code is
         * already inverted), no auto bandwidth, TX gain 51; REG_07 2775 is
         * the 134.4 bit/s clock; REG_08 takes the codeword's low then
         * high 12 bits
         */
        BK4819_writeReg(&c62_bk4819, BK4819_REG_51,
                        BK4819_REG51_TX_CTCDSS_ENABLE | BITV(51, 0));
        BK4819_writeReg(&c62_bk4819, BK4819_REG_07, 2775);
        BK4819_writeReg(&c62_bk4819, BK4819_REG_08, cw & 0xfff);
        BK4819_writeReg(&c62_bk4819, BK4819_REG_08,
                        0x8000 | ((cw >> 12) & 0xfff));
        return;
    }
#endif
    if (transmit)
        bk4819_enable_tx_ctcss(&c62_bk4819, tone);
    else
        bk4819_enable_rx_ctcss(&c62_bk4819, tone);
}

bool radio_checkRxDigitalSquelch()
{
#ifdef CONFIG_DCS
    /*
     * DCS: the code programmed, received as it is (REG_0C bit 14) or
     * inverted (bit 15). Both open the squelch: the discriminator's
     * polarity decides which one a code arrives as.
     */
    if (config != NULL && config->rxToneEn && (config->rxTone & RTX_TONE_DCS))
        return (BK4819_readReg(&c62_bk4819, BK4819_REG_0C)
                & (BK4819_FLAG_CDCSS_PCR | BK4819_FLAG_CDCSS_PNR))
            != 0;
#endif
    return bk4819_get_ctcss(&c62_bk4819);
}

void radio_enableAfOutput()
{
    if (flatMode(radioMode))
        BK4819_SetAF(&c62_bk4819, BK4819_AF_TYPE_BYPASS);
    else
        bk4819_set_modulation(&c62_bk4819, true);
}

void radio_disableAfOutput()
{
    BK4819_SetAF(&c62_bk4819, 0);
}

void radio_enableRx()
{
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    //Before enable RX path delay to allow hardware to settle
    delayMs(5);

    rxVhf = vhfLna(config->rxFrequency);
    if (rxVhf) {
        gpioDev_set(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    } else {
        gpioDev_set(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    }

    bk4819_set_freq(&c62_bk4819, config->rxFrequency + rxOffset);

    if (flatMode(radioMode)) {
        applyM17Fields(false);
    } else {
        bk4819_SetFilterBandwidth(&c62_bk4819, config->bandwidth);
        if (config->rxToneEn)
            setTone(config->rxTone, false);
    }

    bk4819_rx_on(&c62_bk4819);
    radioStatus = RX;
}

void radio_enableTx()
{
    if (config->txDisable == 1 || radioMode == OPMODE_TETRA)
        return;

    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    //Before enable TX path delay to allow hardware to settle
    delayMs(5);

    if (config->txFrequency < 136000000 || config->txFrequency > 600000000)
        return;

    bk4819_set_freq(&c62_bk4819, config->txFrequency);

    if (flatMode(radioMode))
        applyM17Fields(true);
    else if (config->txToneEn)
        setTone(config->txTone, true);

    if (config->txFrequency < 174000000) {
        gpioDev_set(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    } else {
        gpioDev_set(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    }

    gpioDev_set(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    // depending on power level set PWM duty cycle for APC voltage control
    // Need table for this instead of quadratic mapping,
    // and also consider frequency dependence of PA efficiency
    // and battery voltage, but for now just use a crude quadratic mapping
    uint8_t power_percent =
        std::min(config->txPower * config->txPower * 100 / (5000 * 5000), 100U);
    if (power_percent > 60)
        power_percent = 60; // limit to 60% duty cycle for safety
    set_tx_power(power_percent);

    bk4819_tx_on(&c62_bk4819);
    radioStatus = TX;
}

void radio_disableRtx()
{
    bk4819_disable_ctdcss(&c62_bk4819);

    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    bk4819_rtx_off(&c62_bk4819);
    radioStatus = OFF;
}

void radio_updateConfiguration()
{
    /*
     * TETRA receive: the frequency is the only setting that matters (fixed
     * flat audio path, no tones), and call following retunes it within a
     * timeslot (14.17 ms). On the same LNA, set the synthesizer alone,
     * without the LNA switching and settling delay of radio_enableRx().
     */
    if (radioStatus == RX && radioMode == OPMODE_TETRA
        && vhfLna(config->rxFrequency) == rxVhf) {
        bk4819_set_freq(&c62_bk4819, config->rxFrequency + rxOffset);
        return;
    }

    /*
     * Update VCO frequency and tuning parameters if current operating status
     * is different from OFF.
     * This is done by calling again the corresponding functions, which is safe
     * to do and avoids code duplication.
     */
    if (radioStatus == RX)
        radio_enableRx();
    if (radioStatus == TX)
        radio_enableTx();
}

rssi_t radio_getRssi()
{
#ifdef C62_REG43_SWEEP
    /* Called by the rtx thread in RX, so it can own the BK4819 here */
    if (flatMode(radioMode) && radioStatus == RX) {
        int64_t now = k_uptime_get();
        bool recording = true;
#ifdef C62_BASEBAND_DUMP
        recording = baseband_dump_recording();
#endif
        if (reg43SweepStarted && recording)
            reg43SweepElapsed += now - reg43SweepLast;
        reg43SweepLast = now;
        if (!reg43SweepStarted || reg43SweepElapsed >= C62_REG43_SWEEP_MS) {
            if (reg43SweepStarted)
                reg43SweepIndex = (reg43SweepIndex + 1)
                                % ARRAY_SIZE(reg43Sweep);
            reg43SweepStarted = true;
            reg43SweepElapsed = 0;
            c62_rx_reg43 = reg43Sweep[reg43SweepIndex];
            BK4819_writeReg(&c62_bk4819, BK4819_REG_43, c62_rx_reg43);
        }
    }
#endif
    return bk4819_get_rssi(&c62_bk4819);
}

#ifdef CONFIG_FAST_COPY
/*
 * Fast copy, with the BK4819's frequency scanner (REG_32, result in REG_0D
 * and REG_0E, in 10 Hz units) and its CTCSS/DCS scanner (REG_68 to 6A), as
 * the Quansheng UV-K5 uses them.
 */
#define FREQ_SCAN_ON ((290u << 1) | 1u) /* Scan time 0.2 s */
#define FREQ_SCAN_OFF (290u << 1)

void radio_fastCopyStart()
{
    /* Front end off: only a transmitter right next to the radio */
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    BK4819_SetAF(&c62_bk4819, 0);
    BK4819_writeReg(&c62_bk4819, BK4819_REG_32, FREQ_SCAN_OFF);
    BK4819_writeReg(&c62_bk4819, BK4819_REG_32, FREQ_SCAN_ON);
}

bool radio_fastCopyFreq(uint32_t *hz)
{
    uint16_t high = BK4819_readReg(&c62_bk4819, BK4819_REG_0D);
    if (high & 0x8000)
        return false; /* Still scanning */

    uint16_t low = BK4819_readReg(&c62_bk4819, BK4819_REG_0E);
    *hz = ((((uint32_t)high & 0x7ff) << 16) | low) * 10;
    BK4819_writeReg(&c62_bk4819, BK4819_REG_32, FREQ_SCAN_OFF);
    return true;
}

void radio_fastCopyTone(uint32_t hz)
{
    BK4819_writeReg(&c62_bk4819, BK4819_REG_32, FREQ_SCAN_OFF);
    bk4819_set_freq(&c62_bk4819, hz);
    /* CTCSS/DCS generator off, detectors on: they report what they hear */
    BK4819_writeReg(&c62_bk4819, BK4819_REG_51, 0);
    bk4819_rx_on(&c62_bk4819);
}

bool radio_fastCopyToneResult(uint16_t *tone)
{
    uint16_t high = BK4819_readReg(&c62_bk4819, BK4819_REG_69);
    if ((high & 0x8000) == 0) {
        uint16_t low = BK4819_readReg(&c62_bk4819, BK4819_REG_6A);
        uint32_t cw = (((uint32_t)high & 0xfff) << 12) | (low & 0xfff);
        bool inv = false;
        int code = dcsDecode(cw, &inv);
        if (code < 0)
            return false;
        *tone = (uint16_t)(RTX_TONE_DCS | (inv ? RTX_TONE_DCS_INV : 0) | code);
        return true;
    }

    uint16_t ctc = BK4819_readReg(&c62_bk4819, BK4819_REG_68);
    if ((ctc & 0x8000) == 0) {
        /* Tenths of Hz */
        *tone = (uint16_t)(((uint32_t)(ctc & 0x1fff) * 4843) / 10000);
        return true;
    }
    return false;
}

void radio_fastCopyStop()
{
    BK4819_writeReg(&c62_bk4819, BK4819_REG_32, FREQ_SCAN_OFF);
    BK4819_writeReg(&c62_bk4819, BK4819_REG_51, 0);
}
#endif

#ifdef CONFIG_BCAST_FM
/* Band 0 of the BK1080: 87.5 to 108 MHz */
#define BCAST_BAND 0

void radio_bcastPower(uint32_t hz)
{
    BK1080_Init(&c62_bk1080, hz, BCAST_BAND);
}

void radio_bcastTune(uint32_t hz)
{
    BK1080_SetFrequency(&c62_bk1080, hz, BCAST_BAND);
}

void radio_bcastMute(bool mute)
{
    BK1080_Mute(&c62_bk1080, mute);
}

void radio_bcastSignal(uint8_t *rssi, uint8_t *snr, bool *railed)
{
    uint16_t r10 = BK1080_ReadRegister(&c62_bk1080, BK1080_REG_10);
    uint16_t r07 = BK1080_ReadRegister(&c62_bk1080, BK1080_REG_07);
    *rssi = (uint8_t)BK1080_REG_10_GET_RSSI(r10);
    *snr = (uint8_t)BK1080_REG_07_GET_SNR(r07);
    *railed = (r10 & BK1080_REG_10_MASK_AFCRL) != 0;
}
#endif

#ifdef CONFIG_SPECTRUM
/*
 * Spectrum view: retune and read REG_67, as the UV-K5's spectrum mod does.
 * The RSSI needs a moment on the new frequency once the synthesizer has
 * locked.
 */
#define SPECTRUM_SETTLE_US 1500

static bool spectrumVhf; /* LNA in use for the spectrum */

void radio_spectrumStart()
{
    BK4819_SetAF(&c62_bk4819, 0);
    bk4819_disable_ctdcss(&c62_bk4819);
    bk4819_SetFilterBandwidth(&c62_bk4819, BW_12_5);
    spectrumVhf = rxVhf;
}

rssi_t radio_spectrumRssi(uint32_t hz)
{
    bool vhf = vhfLna(hz);
    if (vhf != spectrumVhf) {
        gpioDev_clear(&c62_bk4819_gpio,
                      vhf ? GPIO_UHF_RX_LNA : GPIO_VHF_RX_LNA);
        gpioDev_set(&c62_bk4819_gpio, vhf ? GPIO_VHF_RX_LNA : GPIO_UHF_RX_LNA);
        spectrumVhf = vhf;
    }
    bk4819_set_freq(&c62_bk4819, hz + rxOffset);
    delayUs(SPECTRUM_SETTLE_US);
    return bk4819_get_rssi(&c62_bk4819);
}

void radio_spectrumStop()
{
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
}
#endif

void radio_setTxTone(uint16_t hz)
{
    if (radioStatus != TX)
        return;

    if (hz == 0) {
        BK4819_writeReg(&c62_bk4819, BK4819_REG_70, 0);
        return;
    }

    /* The tone generator, added to the modulation: REG_71 is the frequency
     * in units of 0.0954 Hz, REG_70 its level */
    BK4819_writeReg(&c62_bk4819, BK4819_REG_71,
                    (uint16_t)(((uint32_t)hz * 1048576u + 50000u) / 100000u));
    BK4819_writeReg(&c62_bk4819, BK4819_REG_70,
                    BK4819_REG_70_MASK_ENABLE_TONE1
                        | (66u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
}

enum opstatus radio_getStatus()
{
    return radioStatus;
}

extern "C" void c62_setRxOffset(int32_t hz)
{
    rxOffset = hz;
    if (radioStatus == RX && config != NULL)
        bk4819_set_freq(&c62_bk4819, config->rxFrequency + hz);
}

extern "C" int32_t c62_getRxOffset(void)
{
    return rxOffset;
}

#ifdef CONFIG_TETRA_AFC
void radio_setRxOffset(int32_t hz)
{
    c62_setRxOffset(hz);
}
#endif
