/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_APRS_H
#define OPENRTX_APRS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "protocols/APRS/Ax25.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * APRS packets (APRS 1.0.1): what a frame says, and the frames this radio
 * sends.
 */

/** Positions are in millionths of a degree, north and east positive */
#define APRS_NO_COURSE -1
#define APRS_NO_SPEED -1

enum aprsType {
    APRS_OTHER = 0, /**< Not understood, or nothing shown */
    APRS_POSITION,  /**< A station's position */
    APRS_STATUS,    /**< A status text */
    APRS_MESSAGE,   /**< A message to a station */
    APRS_OBJECT,    /**< An object or item placed by a station */
};

struct aprsPacket {
    char src[10];       /**< Sender: "VA3ABC-7" */
    char via[10];       /**< Last digipeater that repeated it, "" if direct */
    uint8_t type;       /**< enum aprsType */
    bool hasPos;        /**< lat and lon are valid */
    int32_t lat;        /**< Millionths of a degree, north positive */
    int32_t lon;        /**< Millionths of a degree, east positive */
    char symTable;      /**< '/' or '\\' or an overlay character */
    char symCode;       /**< Symbol: '>' car, '-' house... */
    int16_t course;     /**< Degrees, APRS_NO_COURSE if unknown */
    int16_t speed;      /**< Knots, APRS_NO_SPEED if unknown */
    char name[10];      /**< Object or item name */
    char addressee[10]; /**< Message: to whom */
    char msgId[6];      /**< Message number, "" if none */
    char text[68];      /**< Comment, status or message text */
};

/**
 * Read an APRS packet from a frame.
 *
 * @return false if the frame is not one.
 */
bool aprs_parse(const struct ax25Frame *f, struct aprsPacket *out);

/**
 * Write a position report, no timestamp, no messaging:
 * "!4339.19N/07922.99W>comment".
 *
 * @return its length.
 */
size_t aprs_formatPosition(char *buf, size_t len, int32_t lat, int32_t lon,
                           char symTable, char symCode, const char *comment);

/** Symbols a beacon can show */
#define APRS_SYMBOLS 8

/**
 * A symbol to choose for the beacon.
 *
 * @param i: 0 to APRS_SYMBOLS - 1; 0, a person, is the default.
 * @param table: its table, '/'.
 * @param code: its code.
 * @return its name: "Person".
 */
const char *aprs_symbol(uint8_t i, char *table, char *code);

/** @return the name of a symbol heard, "" if not one of the common ones. */
const char *aprs_symbolName(char table, char code);

/** Paths a beacon can take */
enum aprsPath {
    APRS_PATH_WIDE1_WIDE2 = 0, /**< WIDE1-1,WIDE2-1: mobile, the default */
    APRS_PATH_WIDE2,           /**< WIDE2-1: a fixed station */
    APRS_PATH_NONE,            /**< Direct only */
    APRS_PATHS
};

/** @return a path written: "WIDE1-1,WIDE2-1". */
const char *aprs_pathText(uint8_t path);

/**
 * Build the frame of a position beacon, with its check sequence.
 *
 * @param out: where to write it.
 * @param max: room in out.
 * @param call: own callsign, "VA3ABC-7".
 * @param path: enum aprsPath.
 * @return its length, 0 if the callsign is not one.
 */
size_t aprs_beaconFrame(uint8_t *out, size_t max, const char *call,
                        uint8_t path, int32_t lat, int32_t lon, char symTable,
                        char symCode, const char *comment);

/**
 * Distance and bearing from one position to another.
 *
 * @param meters: distance.
 * @param bearing: degrees from north, 0 to 359.
 */
void aprs_distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2,
                   uint32_t *meters, uint16_t *bearing);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_APRS_H */
