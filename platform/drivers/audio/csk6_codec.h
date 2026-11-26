/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSK6_CODEC_H
#define CSK6_CODEC_H

#include <interfaces/audio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * C62 audio through the CSK6 codec and DSP: one 48 kHz stereo stream each way,
 * microphone/speaker on the left and the BK4819 on the right. The device
 * configuration is a bool, true for capture.
 */
extern const struct audioDriver csk6_codec_audio_driver;

/**
 * Configure the codec and start the DSP audio worker.
 * @return zero on success, a negative error code on failure.
 */
int csk6codec_init(void);

/** Cancel streams, join the worker and release DSP audio resources. */
void csk6codec_terminate(void);

/**
 * Enable or disable DSP audio for a board-selected route.
 * The caller handles GPIOs and radio AF muxing and ensures route compatibility.
 *
 * @param source: audio source.
 * @param sink: audio destination.
 * @param enabled: true to enable, false to disable and cancel its stream.
 */
void csk6codec_setRoute(enum AudioSource source, enum AudioSink sink,
                        bool enabled);

/**
 * Keep the microphone captured without a route to it, for its level (VOX).
 *
 * @param on: true to keep capturing.
 */
void csk6codec_setMicMonitor(bool on);

/**
 * @return the microphone level, 0 to 255: the peak of the last 10 ms,
 * falling over about 0.15 s. 0 while the microphone is not captured.
 */
uint8_t csk6codec_getMicLevel(void);

/**
 * Keep the received FM audio heard for instant replay (CONFIG_REPLAY).
 *
 * @param on: true while the channel is heard.
 */
void csk6codec_setRxRecord(bool on);

/**
 * AI noise reduction of FM audio (CONFIG_NOISE_REDUCTION).
 *
 * @param rx: level for received audio, 0 off, 1 to 10.
 * @param tx: level for the microphone, 0 off, 1 to 10.
 */
void csk6codec_setNoiseReduction(uint8_t rx, uint8_t tx);

/**
 * @param overload: set to true if the noise reduction was too slow and is
 * bypassed until a level changes. May be NULL.
 * @return the time it takes, in percent of the audio's time.
 */
uint8_t csk6codec_getNoiseReductionLoad(bool *overload);

/**
 * CONFIG_APRS: hand the received FM audio to the APRS decoder.
 *
 * @param on: true to.
 */
void csk6codec_setAprsRx(bool on);

/**
 * @return rounds of the codec worker so far, about one a millisecond while
 * it runs (self test).
 */
uint32_t csk6codec_loops(void);

/**
 * @return the result of opening the DSP audio streams: 0, or an error.
 */
int csk6codec_initResult(void);

#ifdef __cplusplus
}
#endif

#endif /* CSK6_CODEC_H */
