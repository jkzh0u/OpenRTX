/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_APRS_STATUS_H
#define OPENRTX_APRS_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * APRS heard in FM (CONFIG_APRS): the received audio goes in from the audio
 * driver, the rtx thread decodes it, the UI reads the stations and messages
 * heard. Kept until the radio is turned off.
 */

/** Stations and objects kept, the oldest heard is dropped for a new one */
#define APRS_STATIONS 24
/** Messages kept, newest first */
#define APRS_MESSAGES 12
/** Received audio waiting to be decoded, samples at AFSK_RX_RATE */
#define APRS_RX_RING 4096

struct aprsStation {
    char call[10];    /**< Callsign, or the name of an object */
    char by[10];      /**< Object: the station that placed it */
    char via[10];     /**< Digipeater last heard through, "" direct */
    long long first;  /**< First heard, ms */
    long long last;   /**< Last heard, ms */
    uint16_t heard;   /**< Packets heard, repeats not counted */
    uint8_t type;     /**< enum aprsType of the last packet */
    bool hasPos;
    int32_t lat, lon; /**< Millionths of a degree */
    char symTable, symCode;
    int16_t course;   /**< Degrees, APRS_NO_COURSE */
    int16_t speed;    /**< Knots, APRS_NO_SPEED */
    char text[44];    /**< Comment or status */
    uint32_t hash;    /**< Of the last packet, to spot repeats */
};

struct aprsMessage {
    char from[10];
    char to[10];
    char id[6];
    char text[68];
    long long time;
};

/**
 * Received FM audio, from the audio driver's thread: kept for the rtx
 * thread. Samples that do not fit are dropped.
 *
 * @param pcm: samples at AFSK_RX_RATE.
 */
void aprsStatus_pushAudio(const int16_t *pcm, size_t n);

/**
 * Decode the audio received so far, from the rtx thread.
 *
 * @param now: current time, ms.
 */
void aprsStatus_process(long long now);

/** Note a frame received, decoded (from aprsStatus_process, and tests). */
void aprsStatus_frame(const uint8_t *frame, size_t len, long long now);

/** @return packets decoded since power on. */
uint32_t aprsStatus_packets(void);

/** @return the number of stations kept. */
uint8_t aprsStatus_stationCount(void);

/**
 * @param n: 0 for the last heard.
 * @return false past the oldest.
 */
bool aprsStatus_station(uint8_t n, struct aprsStation *out);

/** @return the number of messages kept. */
uint8_t aprsStatus_messageCount(void);

/**
 * @param n: 0 for the newest.
 * @return false past the oldest.
 */
bool aprsStatus_message(uint8_t n, struct aprsMessage *out);

/** Ask for a beacon to be sent, from the UI. */
void aprsStatus_requestBeacon(void);

/** @return true from a request until the beacon is sent. */
bool aprsStatus_beaconRequested(void);

/** Note a beacon sent, which answers any request. */
void aprsStatus_beaconSent(long long now);

/** @return when the last beacon was sent, ms, 0 if never. */
long long aprsStatus_lastBeacon(void);

/** Forget everything heard. */
void aprsStatus_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_APRS_STATUS_H */
