/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/platform.h"
#include "interfaces/delays.h"
#include "hwconfig.h"
#include "interfaces/radio.h"
#include "core/state.h"
#include "core/roger_beep.h"
#include "core/fast_copy.h"
#include "core/spectrum.h"
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include <string.h>
#include "rtx/OpMode_FM.hpp"
#include "rtx/rtx.h"

#if defined(PLATFORM_TTWRPLUS)
#include "drivers/baseband/AT1846S.h"
#endif

/**
 * \internal
 * On MD-UV3x0 radios the volume knob does not regulate the amplitude of the
 * analog signal towards the audio amplifier but it rather serves to provide a
 * digital value to be fed into the HR_C6000 lineout DAC gain. We thus have to
 * provide the helper function below to keep the real volume level consistent
 * with the knob position.
 */
#if defined(PLATFORM_TTWRPLUS)
void _setVolume()
{
    static uint8_t oldVolume = 0xFF;
    uint8_t volume = platform_getVolumeLevel();

    if(volume == oldVolume)
        return;

    // AT1846S volume control is 4 bit
    AT1846S::instance().setRxAudioGain(volume / 16, volume / 16);
    oldVolume = volume;
}
#endif

OpMode_FM::OpMode_FM() : rfSqlOpen(false), sqlOpen(false), enterRx(true)
{
}

OpMode_FM::~OpMode_FM()
{
}

void OpMode_FM::enable()
{
    // When starting, close squelch and prepare for entering in RX mode.
    rfSqlOpen = false;
    sqlOpen   = false;
    enterRx   = true;
}

void OpMode_FM::disable()
{
    // Clean shutdown.
    platform_ledOff(GREEN);
    platform_ledOff(RED);
    audioPath_release(rxAudioPath);
    audioPath_release(txAudioPath);
    radio_disableRtx();
    rfSqlOpen = false;
    sqlOpen   = false;
    enterRx   = false;
}

#ifdef CONFIG_ROGER_BEEP
/**
 * Play a talk permit tone in the radio's own speaker, blocking until it
 * ends, before the transmitter keys up.
 *
 * @param style: talk permit style, 0 for none.
 */
static void playTalkPermit(uint8_t style)
{
    static int16_t tone[TALK_PERMIT_MAX_SAMPLES];
    static stream_sample_t buf[2 * 240]; // Two halves of 30 ms
    const size_t half = sizeof(buf) / sizeof(buf[0]) / 2;

    size_t n = talkPermit_render(style, tone, TALK_PERMIT_MAX_SAMPLES);
    if (n == 0)
        return;

    pathId path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_PROMPT);
    if (audioPath_getStatus(path) != PATH_OPEN) {
        audioPath_release(path);
        return;
    }

    memset(buf, 0, sizeof(buf));
    streamId stream = audioStream_start(path, buf, 2 * half, TALK_PERMIT_RATE,
                                        STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (stream < 0) {
        audioPath_release(path);
        return;
    }

    outputStream_sync(stream, false);
    // The tone, then two halves of silence so that all of it plays
    for (size_t pos = 0; pos < n + 2 * half; pos += half) {
        stream_sample_t *out = outputStream_getIdleBuffer(stream);
        if (out == NULL)
            break;
        for (size_t i = 0; i < half; i++)
            out[i] = (pos + i < n) ? tone[pos + i] : 0;
        outputStream_sync(stream, true);
    }

    audioStream_stop(stream);
    audioPath_release(path);
}
#endif

void OpMode_FM::update(rtxStatus_t *const status, const bool newCfg)
{
    (void) newCfg;

    #if defined(CONFIG_FAST_COPY) || defined(CONFIG_SPECTRUM)
    // Fast copy or the spectrum view has the radio: no reception, no
    // transmission
    static bool borrowed;
    bool borrow = false;
    #ifdef CONFIG_FAST_COPY
    borrow = fastCopy_run(getTick());
    #endif
    #ifdef CONFIG_SPECTRUM
    if(!borrow)
        borrow = spectrum_run();
    #endif
    if(borrow)
    {
        if(sqlOpen)
        {
            audioPath_release(rxAudioPath);
            sqlOpen = false;
        }
        borrowed = true;
        sleepFor(0u, 10u);
        return;
    }
    if(borrowed)
    {
        // Receive normally again, with the configuration's settings
        borrowed = false;
        radio_disableRtx();
        status->opStatus = OFF;
        enterRx = true;
    }
    #endif

    #if defined(PLATFORM_TTWRPLUS)
    // Set output volume by changing the HR_C6000 DAC gain
    _setVolume();
    #endif

    // RX logic
    if(status->opStatus == RX)
    {
        // RF squelch mechanism
        // This turns squelch (0 to 15) into RSSI (-127.0dbm to -61dbm)
        rssi_t squelch = -127 + (status->sqlLevel * 66) / 15;
        rssi_t rssi    = rtx_getRssi();

        // Provide a bit of hysteresis, only change state if the RSSI has
        // moved more than 1dBm on either side of the current squelch setting.
        if((rfSqlOpen == false) && (rssi > (squelch + 1))) rfSqlOpen = true;
        if((rfSqlOpen == true)  && (rssi < (squelch - 1))) rfSqlOpen = false;

        // Local flags for current RF and tone squelch status
        bool rfSql   = ((status->rxToneEn == 0) && (rfSqlOpen == true));
        bool toneSql = ((status->rxToneEn == 1) && radio_checkRxDigitalSquelch());

        // Audio control
        if((sqlOpen == false) && (rfSql || toneSql))
        {
            rxAudioPath = audioPath_request(SOURCE_RTX, SINK_SPK, PRIO_RX);
            if(rxAudioPath > 0) sqlOpen = true;
        }

        if((sqlOpen == true) && (rfSql == false) && (toneSql == false))
        {
            audioPath_release(rxAudioPath);
            sqlOpen = false;
        }
    }
    else if((status->opStatus == OFF) && enterRx)
    {
        radio_disableRtx();

        radio_enableRx();
        status->opStatus = RX;
        enterRx = false;
    }

    // TX logic
    if(platform_getPttStatus() && (status->opStatus != TX) &&
                                  (status->txDisable == 0))
    {
        audioPath_release(rxAudioPath);
        radio_disableRtx();

        #ifdef CONFIG_ROGER_BEEP
        playTalkPermit(state.settings.talkPermit);
        #endif

        txAudioPath = audioPath_request(SOURCE_MIC, SINK_RTX, PRIO_TX);
        radio_enableTx();

        status->opStatus = TX;
    }

    if(!platform_getPttStatus() && (status->opStatus == TX))
    {
        #ifdef CONFIG_ROGER_BEEP
        rogerBeep_play(state.settings.rogerBeep, radio_setTxTone);
        #endif

        audioPath_release(txAudioPath);
        radio_disableRtx();

        status->opStatus = OFF;
        enterRx = true;
        sqlOpen = false;  // Force squelch to be redetected.
    }

    // Led control logic
    switch(status->opStatus)
    {
        case RX:
            if(radio_checkRxDigitalSquelch())
            {
                platform_ledOn(GREEN);  // Red + green LEDs ("orange"): tone squelch open
                platform_ledOn(RED);
            }
            else if(rfSqlOpen)
            {
                platform_ledOn(GREEN);  // Green LED only: RF squelch open
                platform_ledOff(RED);
            }
            else
            {
                platform_ledOff(GREEN);
                platform_ledOff(RED);
            }

            break;

        case TX:
            platform_ledOff(GREEN);
            platform_ledOn(RED);
            break;

        default:
            platform_ledOff(GREEN);
            platform_ledOff(RED);
            break;
    }

    // Sleep thread for 30ms for 33Hz update rate
    sleepFor(0u, 30u);
}

bool OpMode_FM::rxSquelchOpen()
{
    return sqlOpen;
}
