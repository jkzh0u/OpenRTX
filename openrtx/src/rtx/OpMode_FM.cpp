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
#include "core/vox.h"
#include "core/bcast.h"
#include "core/weather.h"
#include "interfaces/audio.h"
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include <string.h>
#include <math.h>
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

/*
 * The receive audio path (rxAudioPath) is held while the channel is heard
 * or, with CONFIG_BCAST_FM, while the broadcast radio plays: sqlOpen only
 * tells the channel's squelch.
 */
static bool audioOpen;
/* The transceiver's own audio on (the path turns it on when opened) */
static bool afOn;
/* Updates for which the squelch stays shut: after a weather look, the
 * RSSI filter still holds the weather channel's signal */
static uint8_t holdSql;

#ifdef CONFIG_VOX
static bool voxKeyed;       // VOX has the transmitter keyed
static bool voxMonitoring;  // The microphone is kept captured
static bool voxWaitQuiet;   // After VOX_MAX_MS: not again until quiet
static long long voxLoud;   // Last time the microphone was loud enough
static long long voxStart;  // When VOX keyed up
static long long lastRxOpen; // Last time the squelch was open

/**
 * VOX, on each update: keys up when the microphone is loud enough, not while
 * receiving or just after, and unkeys after the hang time without speech or
 * after VOX_MAX_MS.
 *
 * @return true while VOX wants the transmitter keyed.
 */
static bool voxUpdate(const rtxStatus_t *status, bool sqlOpen)
{
    uint8_t level = state.settings.voxLevel;
    long long now = getTick();
    bool on = (level != 0) && (status->txDisable == 0);

    if(on != voxMonitoring)
    {
        audio_setMicMonitor(on);
        voxMonitoring = on;
    }
    if(!on)
    {
        voxKeyed = false;
        voxWaitQuiet = false;
        return false;
    }

    if(sqlOpen)
        lastRxOpen = now;

    bool loud = platform_getMicLevel() >= vox_threshold(level);
    if(voxKeyed)
    {
        if(loud)
            voxLoud = now;
        if((now - voxLoud) > (long long) vox_hangMs(state.settings.voxDelay))
            voxKeyed = false;
        else if((now - voxStart) > VOX_MAX_MS)
        {
            voxKeyed = false;
            voxWaitQuiet = true;
        }
        return voxKeyed;
    }

    if(voxWaitQuiet)
    {
        if(!loud)
            voxWaitQuiet = false;
        return false;
    }

    if(loud && (status->opStatus == RX) && !sqlOpen
       && ((now - lastRxOpen) > VOX_RX_GUARD_MS))
    {
        voxKeyed = true;
        voxLoud = now;
        voxStart = now;
    }
    return voxKeyed;
}
#endif

void OpMode_FM::disable()
{
    // Clean shutdown.
    platform_ledOff(GREEN);
    platform_ledOff(RED);
    audioPath_release(rxAudioPath);
    audioPath_release(txAudioPath);
    audioOpen = false;
    afOn      = false;
    #ifdef CONFIG_WEATHER
    wx_update(0, false, getTick());
    #endif
    #ifdef CONFIG_BCAST_FM
    bcast_halt();
    #endif
    radio_disableRtx();
    rfSqlOpen = false;
    sqlOpen   = false;
    enterRx   = false;
    #ifdef CONFIG_NOISE_REDUCTION
    audio_setNoiseReduction(0, 0);
    #endif
    #ifdef CONFIG_VOX
    if(voxMonitoring)
        audio_setMicMonitor(false);
    voxMonitoring = false;
    voxKeyed = false;
    #endif
}

#if defined(CONFIG_ROGER_BEEP) || defined(CONFIG_WEATHER)
/* Local sounds: 8 kHz */
#define LOCAL_RATE 8000
/* Longest local sound: 0.3 s */
#define LOCAL_MAX_SAMPLES (LOCAL_RATE * 3 / 10)

/* The sound being played, shared by all of them */
static int16_t localSound[LOCAL_MAX_SAMPLES];

/**
 * Play a sound in the radio's own speaker, blocking until it ends.
 *
 * @param tone: samples at LOCAL_RATE.
 * @param n: number of samples.
 */
static void playLocal(const int16_t *tone, size_t n)
{
    static stream_sample_t buf[2 * 240]; // Two halves of 30 ms
    const size_t half = sizeof(buf) / sizeof(buf[0]) / 2;

    if (n == 0)
        return;

    pathId path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_PROMPT);
    if (audioPath_getStatus(path) != PATH_OPEN) {
        audioPath_release(path);
        return;
    }

    memset(buf, 0, sizeof(buf));
    streamId stream = audioStream_start(path, buf, 2 * half, LOCAL_RATE,
                                        STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (stream < 0) {
        audioPath_release(path);
        return;
    }

    outputStream_sync(stream, false);
    // The sound, then two halves of silence so that all of it plays
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

#ifdef CONFIG_ROGER_BEEP
/**
 * Play a talk permit tone in the radio's own speaker, blocking until it
 * ends, before the transmitter keys up.
 *
 * @param style: talk permit style, 0 for none.
 */
static void playTalkPermit(uint8_t style)
{
    size_t n = talkPermit_render(style, localSound, LOCAL_MAX_SAMPLES);
    playLocal(localSound, n);
}
#endif

#ifdef CONFIG_WEATHER
/* The alarm repeats every this often, for at most this long */
#define WX_ALARM_PERIOD_MS 4000
#define WX_ALARM_MAX_MS 120000

/**
 * The weather alarm in the radio's own speaker: the attention signal of US
 * emergency alerts, 853 and 960 Hz together, three bursts of 0.25 s.
 */
static void playWeatherAlarm()
{
    const size_t n = LOCAL_RATE / 4;

    for (size_t i = 0; i < n; i++) {
        float t = (float) i / LOCAL_RATE;
        localSound[i] = (int16_t) (9000.0f * (sinf(2.0f * 3.14159265f * 853.0f * t)
                                            + sinf(2.0f * 3.14159265f * 960.0f * t)));
    }
    for (int k = 0; k < 3; k++) {
        playLocal(localSound, n);
        sleepFor(0u, 100u);
    }
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
        if(audioOpen)
        {
            audioPath_release(rxAudioPath);
            audioOpen = false;
            afOn = false;
        }
        sqlOpen = false;
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

    #ifdef CONFIG_WEATHER
    // Weather alert watch: a short look at the weather channel now and then,
    // only while nothing is heard and the radio is not keyed
    bool wxIdle = (status->opStatus == RX) && !audioOpen
               && !platform_getPttStatus();
    if(wx_update(state.settings.wxAlert, wxIdle, getTick()))
    {
        holdSql = 3;
        sleepFor(0u, 10u);
        return;
    }

    // An alert heard: the alarm until it is seen, or for 2 minutes
    static long long alarmFirst = -1, alarmLast;
    if(wx_alert() >= 0 && status->opStatus != TX)
    {
        long long now = getTick();
        if(alarmFirst < 0)
            alarmFirst = now;
        if((now - alarmFirst < WX_ALARM_MAX_MS)
           && (now - alarmLast >= WX_ALARM_PERIOD_MS))
        {
            playWeatherAlarm();
            alarmLast = getTick();
        }
    }
    else if(wx_alert() < 0)
    {
        alarmFirst = -1;
    }
    #endif

    // RX logic
    bool chanOpen = false;
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

        if(holdSql > 0)
        {
            holdSql--;
            rfSqlOpen = false;
        }

        // Local flags for current RF and tone squelch status
        bool rfSql   = ((status->rxToneEn == 0) && (rfSqlOpen == true));
        bool toneSql = ((status->rxToneEn == 1) && radio_checkRxDigitalSquelch());
        chanOpen = rfSql || toneSql;
    }
    else if((status->opStatus == OFF) && enterRx)
    {
        radio_disableRtx();

        radio_enableRx();
        status->opStatus = RX;
        enterRx = false;
    }

    // The broadcast radio, muted while the channel is heard or keyed
    bool bcastPlay = false;
    #ifdef CONFIG_BCAST_FM
    bcastPlay = bcast_update(chanOpen, status->opStatus == TX, getTick())
             && (status->opStatus == RX);
    #endif

    // Audio control
    if(status->opStatus == RX)
    {
        bool want = chanOpen || bcastPlay;
        if(!audioOpen && want)
        {
            rxAudioPath = audioPath_request(SOURCE_RTX, SINK_SPK, PRIO_RX);
            if(rxAudioPath > 0)
            {
                audioOpen = true;
                afOn = true;
            }
        }

        if(audioOpen && !want)
        {
            audioPath_release(rxAudioPath);
            audioOpen = false;
            afOn = false;
        }

        #ifdef CONFIG_BCAST_FM
        // The transceiver's audio only for the channel: the broadcast radio
        // alone is heard through the same path. Without it the path is only
        // open for the channel, and the audio driver turns that audio on.
        if(audioOpen && (afOn != chanOpen))
        {
            if(chanOpen)
                radio_enableAfOutput();
            else
                radio_disableAfOutput();
            afOn = chanOpen;
        }
        #endif

        sqlOpen = chanOpen && audioOpen;
    }

    #ifdef CONFIG_NOISE_REDUCTION
    // Settings > FM, applied by the audio driver to the FM audio
    audio_setNoiseReduction(state.settings.nrRx, state.settings.nrTx);
    #endif

    // TX logic: PTT, or VOX (not while something is heard: the speaker
    // would key it)
    bool ptt   = platform_getPttStatus();
    bool keyed = ptt;
    #ifdef CONFIG_VOX
    keyed = voxUpdate(status, sqlOpen || bcastPlay) || ptt;
    #endif

    if(keyed && (status->opStatus != TX) && (status->txDisable == 0))
    {
        audioPath_release(rxAudioPath);
        audioOpen = false;
        afOn = false;
        sqlOpen = false;
        radio_disableRtx();

        #ifdef CONFIG_ROGER_BEEP
        // Not for VOX: it would delay the start of what is said
        if(ptt)
            playTalkPermit(state.settings.talkPermit);
        #endif

        txAudioPath = audioPath_request(SOURCE_MIC, SINK_RTX, PRIO_TX);
        radio_enableTx();

        status->opStatus = TX;
    }

    if(!keyed && (status->opStatus == TX))
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
