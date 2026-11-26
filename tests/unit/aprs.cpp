/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "protocols/APRS/Afsk.h"
#include "protocols/APRS/Aprs.h"
#include "protocols/APRS/AprsStatus.h"
#include "protocols/APRS/Ax25.h"

/** A UI frame from text: "SRC>DST,DIGI,DIGI*" and the information */
static ax25Frame makeFrame(const char *src, const char *dst,
                           std::vector<const char *> digis,
                           const std::string &info)
{
    ax25Frame f;
    memset(&f, 0, sizeof(f));
    REQUIRE(ax25_parseCall(src, &f.src));
    REQUIRE(ax25_parseCall(dst, &f.dst));
    for (const char *d : digis) {
        std::string s(d);
        bool rep = !s.empty() && s.back() == '*';
        if (rep)
            s.pop_back();
        REQUIRE(ax25_parseCall(s.c_str(), &f.digi[f.ndigi]));
        f.digi[f.ndigi++].repeated = rep;
    }
    f.control = 0x03;
    f.pid = 0xf0;
    memcpy(f.info, info.data(), info.size());
    f.infoLen = (uint16_t)info.size();
    return f;
}

static aprsPacket parse(const ax25Frame &f)
{
    aprsPacket p;
    REQUIRE(aprs_parse(&f, &p));
    return p;
}

TEST_CASE("AX.25 callsigns", "[aprs][ax25]")
{
    ax25Address a;
    char text[12];

    REQUIRE(ax25_parseCall("va3abc-7", &a));
    REQUIRE(strcmp(a.call, "VA3ABC") == 0);
    REQUIRE(a.ssid == 7);
    ax25_callText(&a, text, sizeof(text));
    REQUIRE(strcmp(text, "VA3ABC-7") == 0);

    REQUIRE(ax25_parseCall("WIDE2", &a));
    REQUIRE(a.ssid == 0);
    REQUIRE_FALSE(ax25_parseCall("TOOLONG1", &a));
    REQUIRE_FALSE(ax25_parseCall("VA3ABC-16", &a));
    REQUIRE_FALSE(ax25_parseCall("-1", &a));
    REQUIRE_FALSE(ax25_parseCall("VA3ABC-", &a));
}

TEST_CASE("AX.25 frames encode and decode", "[aprs][ax25]")
{
    ax25Frame f = makeFrame("VA3ABC-7", "APZRTX", { "VE3RPT*", "WIDE2-1" },
                            "!4339.19N/07922.99W>Hi");
    uint8_t buf[AX25_MAX_FRAME];
    size_t len = ax25_encode(&f, buf, sizeof(buf));
    REQUIRE(len == 7 * 4 + 2 + 22 + 2);

    // The check sequence: CRC-16/X.25 of "123456789" is 0x906E
    REQUIRE(ax25_fcs((const uint8_t *)"123456789", 9) == 0x906e);

    ax25Frame g;
    REQUIRE(ax25_decode(buf, len - 2, &g));
    REQUIRE(strcmp(g.src.call, "VA3ABC") == 0);
    REQUIRE(g.src.ssid == 7);
    REQUIRE(strcmp(g.dst.call, "APZRTX") == 0);
    REQUIRE(g.ndigi == 2);
    REQUIRE(g.digi[0].repeated);
    REQUIRE_FALSE(g.digi[1].repeated);
    REQUIRE(g.infoLen == 22);
    REQUIRE(memcmp(g.info, "!4339.19N/07922.99W>Hi", 22) == 0);
}

/** Received frames, for the modem tests */
static std::vector<std::vector<uint8_t>> received;

static void onFrame(const uint8_t *frame, size_t len, void *user)
{
    (void)user;
    received.emplace_back(frame, frame + len);
}

static uint32_t rng = 1;

static int noise(int amplitude)
{
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 16) % (2 * amplitude + 1)) - amplitude;
}

TEST_CASE("AFSK 1200 sent at 48 kHz, received at 9600 Hz", "[aprs][afsk]")
{
    ax25Frame f = makeFrame("VA3ABC-7", "APZRTX", { "WIDE1-1", "WIDE2-1" },
                            "!4339.19N/07922.99W>OpenRTX ~~~~~~ 0123456789");
    uint8_t frame[AX25_MAX_FRAME];
    size_t len = ax25_encode(&f, frame, sizeof(frame));

    // Treble cut and boosted, as pre- and de-emphasis leave it, with noise
    for (int tilt = -1; tilt <= 1; tilt++) {
        afskMod m;
        afskMod_init(&m, frame, len, 48000, 30, 3, 8000);
        std::vector<int16_t> tx(4800, 0);
        int16_t block[256];
        size_t n;
        while ((n = afskMod_render(&m, block, 256)) > 0)
            tx.insert(tx.end(), block, block + n);
        tx.insert(tx.end(), 4800, 0);

        // As the audio driver does it: 5 samples averaged
        std::vector<int16_t> rx;
        int16_t prev = 0;
        int32_t sum = 0;
        for (size_t i = 0; i < tx.size(); i++) {
            float v = tx[i];
            if (tilt > 0)
                v = tx[i] + 1.5f * (tx[i] - prev);
            else if (tilt < 0)
                v = 0.5f * (tx[i] + prev);
            prev = tx[i];
            sum += (int32_t)v + noise(2000);
            if (i % 5 == 4) {
                rx.push_back((int16_t)(sum / 5));
                sum = 0;
            }
        }

        received.clear();
        afskDemod d;
        afskDemod_init(&d);
        afskDemod_process(&d, rx.data(), rx.size(), onFrame, nullptr);
        REQUIRE(received.size() == 1);
        REQUIRE(received[0].size() == len - 2);
        REQUIRE(memcmp(received[0].data(), frame, len - 2) == 0);
    }
}

TEST_CASE("AFSK: noise alone gives no frame", "[aprs][afsk]")
{
    std::vector<int16_t> rx(96000);
    for (auto &s : rx)
        s = (int16_t)noise(6000);
    received.clear();
    afskDemod d;
    afskDemod_init(&d);
    afskDemod_process(&d, rx.data(), rx.size(), onFrame, nullptr);
    REQUIRE(received.empty());
}

TEST_CASE("APRS positions", "[aprs]")
{
    // Uncompressed, with course and speed, heard through a digipeater
    aprsPacket p = parse(makeFrame("VA3ABC-9", "APRS", { "VE3RPT*", "WIDE2" },
                                   "!4339.19N/07922.99W>088/036Hello"));
    REQUIRE(p.type == APRS_POSITION);
    REQUIRE(p.hasPos);
    REQUIRE(p.lat == 43653166);
    REQUIRE(p.lon == -79383166);
    REQUIRE(p.symTable == '/');
    REQUIRE(p.symCode == '>');
    REQUIRE(p.course == 88);
    REQUIRE(p.speed == 36);
    REQUIRE(strcmp(p.text, "Hello") == 0);
    REQUIRE(strcmp(p.src, "VA3ABC-9") == 0);
    REQUIRE(strcmp(p.via, "VE3RPT") == 0);

    // With a timestamp, south and east
    p = parse(
        makeFrame("VK2ABC", "APRS", {}, "@092345z3351.00S\\15112.50E-Home"));
    REQUIRE(p.lat == -33850000);
    REQUIRE(p.lon == 151208333);
    REQUIRE(p.symTable == '\\');
    REQUIRE(p.course == APRS_NO_COURSE);
    REQUIRE(strcmp(p.text, "Home") == 0);
    REQUIRE(p.via[0] == '\0');

    // Compressed, the example of the specification
    p = parse(makeFrame("N0CALL", "APRS", {}, "=/5L!!<*e7>7P["));
    REQUIRE(p.type == APRS_POSITION);
    REQUIRE(p.lat == 49500000);
    REQUIRE(std::abs(p.lon + 72750000) < 10); // -72.750004 exactly
    REQUIRE(p.symCode == '>');
    REQUIRE(p.course == 88);
    REQUIRE(p.speed == 36);

    // Mic-E: 33 25.64 N in the destination, 079 22.99 W, 20 knots, 251
    std::string info = "`k2";
    info += (char)0x7f;
    info += (char)(28 + 2);
    info += (char)(28 + 2);
    info += (char)(28 + 51);
    info += ">/]Hello";
    p = parse(makeFrame("VA3XYZ-9", "S32U6T", {}, info));
    REQUIRE(p.type == APRS_POSITION);
    REQUIRE(p.lat == 33427333);
    REQUIRE(p.lon == -79383166);
    REQUIRE(p.speed == 20);
    REQUIRE(p.course == 251);
    REQUIRE(p.symCode == '>');
    REQUIRE(p.symTable == '/');
    REQUIRE(strcmp(p.text, "Hello") == 0);
}

TEST_CASE("APRS status, messages and objects", "[aprs]")
{
    aprsPacket p = parse(makeFrame("VE3XX", "APRS", {}, ">Net at 8 pm"));
    REQUIRE(p.type == APRS_STATUS);
    REQUIRE(strcmp(p.text, "Net at 8 pm") == 0);

    p = parse(makeFrame("VE3XX", "APRS", {}, ":VA3ABC-7 :Hello there{42"));
    REQUIRE(p.type == APRS_MESSAGE);
    REQUIRE(strcmp(p.addressee, "VA3ABC-7") == 0);
    REQUIRE(strcmp(p.text, "Hello there") == 0);
    REQUIRE(strcmp(p.msgId, "42") == 0);

    p = parse(makeFrame("VE3XX", "APRS", {},
                        ";LEADER   *092345z4903.50N/07201.75W>088/036"));
    REQUIRE(p.type == APRS_OBJECT);
    REQUIRE(strcmp(p.name, "LEADER") == 0);
    REQUIRE(p.lat == 49058333);
    REQUIRE(p.lon == -72029166);

    p = parse(makeFrame("VE3XX", "APRS", {}, ")AID #2!4903.50N/07201.75WA"));
    REQUIRE(p.type == APRS_OBJECT);
    REQUIRE(strcmp(p.name, "AID #2") == 0);
    REQUIRE(p.hasPos);

    // Not understood: kept as text
    p = parse(makeFrame("VE3XX", "APRS", {}, "T#005,199,000,255"));
    REQUIRE(p.type == APRS_OTHER);
}

TEST_CASE("APRS beacon sent", "[aprs]")
{
    char text[64];
    aprs_formatPosition(text, sizeof(text), 43653200, -79383200, '/', '[',
                        "OpenRTX");
    REQUIRE(strcmp(text, "!4339.19N/07922.99W[OpenRTX") == 0);
    aprs_formatPosition(text, sizeof(text), -33850000, 151208333, '/', '-', "");
    REQUIRE(strcmp(text, "!3351.00S/15112.50E-") == 0);
    // 59.996 minutes round up to the next degree
    aprs_formatPosition(text, sizeof(text), 43999933, 0, '/', '-', "");
    REQUIRE(strncmp(text, "!4400.00N", 9) == 0);

    uint8_t frame[AX25_MAX_FRAME];
    size_t len = aprs_beaconFrame(frame, sizeof(frame), "VA3ABC-7",
                                  APRS_PATH_WIDE1_WIDE2, 43653200, -79383200,
                                  '/', '>', "C62");
    REQUIRE(len > 0);
    REQUIRE(ax25_fcs(frame, len - 2)
            == (uint16_t)(frame[len - 2] | (frame[len - 1] << 8)));
    ax25Frame f;
    REQUIRE(ax25_decode(frame, len - 2, &f));
    REQUIRE(f.ndigi == 2);
    REQUIRE(strcmp(f.digi[0].call, "WIDE1") == 0);
    aprsPacket p = parse(f);
    REQUIRE(strcmp(p.src, "VA3ABC-7") == 0);
    REQUIRE(p.lat == 43653166);
    REQUIRE(p.lon == -79383166);
    REQUIRE(strcmp(p.text, "C62") == 0);

    REQUIRE(aprs_beaconFrame(frame, sizeof(frame), "", APRS_PATH_NONE, 0, 0,
                             '/', '>', "")
            == 0);
}

TEST_CASE("APRS distance and bearing", "[aprs]")
{
    uint32_t m;
    uint16_t b;
    // Toronto to Ottawa: about 352 km, north east
    aprs_distance(43653200, -79383200, 45421500, -75697200, &m, &b);
    REQUIRE(m > 345000);
    REQUIRE(m < 360000);
    REQUIRE(b > 50);
    REQUIRE(b < 62);
}

TEST_CASE("APRS stations and messages heard", "[aprs]")
{
    uint8_t frame[AX25_MAX_FRAME];
    aprsStatus_reset();

    auto hear = [&](const ax25Frame &f, long long t) {
        size_t len = ax25_encode(&f, frame, sizeof(frame));
        aprsStatus_frame(frame, len - 2, t);
    };

    ax25Frame a = makeFrame("VA3ABC-9", "APRS", {},
                            "!4339.19N/07922.99W>Mobile");
    hear(a, 1000);
    hear(a, 2000); // digipeated copy: not counted again
    hear(makeFrame("VE3XX", "APRS", {}, ">QRV"), 3000);
    hear(makeFrame("VE3XX", "APRS", {}, ":VA3ABC-7 :Hi{1"), 4000);
    hear(makeFrame("VE3XX", "APRS", {}, ":VA3ABC-7 :Hi{1"), 9000);

    REQUIRE(aprsStatus_packets() == 5);
    REQUIRE(aprsStatus_stationCount() == 2);
    aprsStation s;
    REQUIRE(aprsStatus_station(0, &s));
    REQUIRE(strcmp(s.call, "VE3XX") == 0);
    REQUIRE(strcmp(s.text, "QRV") == 0);
    REQUIRE(aprsStatus_station(1, &s));
    REQUIRE(strcmp(s.call, "VA3ABC-9") == 0);
    REQUIRE(s.heard == 1);
    REQUIRE(s.hasPos);
    REQUIRE(strcmp(s.text, "Mobile") == 0);
    REQUIRE_FALSE(aprsStatus_station(2, &s));

    // The message sent again is kept once
    REQUIRE(aprsStatus_messageCount() == 1);
    aprsMessage msg;
    REQUIRE(aprsStatus_message(0, &msg));
    REQUIRE(strcmp(msg.from, "VE3XX") == 0);
    REQUIRE(strcmp(msg.to, "VA3ABC-7") == 0);
    REQUIRE(strcmp(msg.text, "Hi") == 0);

    // Through the audio path: a beacon decoded from its sound
    aprsStatus_reset();
    size_t len = aprs_beaconFrame(frame, sizeof(frame), "VA3ABC-7",
                                  APRS_PATH_NONE, 43653200, -79383200, '/', '>',
                                  "Audio");
    afskMod m;
    afskMod_init(&m, frame, len, AFSK_RX_RATE, 30, 3, 8000);
    int16_t block[96];
    size_t n;
    while ((n = afskMod_render(&m, block, 96)) > 0) {
        aprsStatus_pushAudio(block, n);
        aprsStatus_process(5000);
    }
    memset(block, 0, sizeof(block));
    aprsStatus_pushAudio(block, 96);
    aprsStatus_process(5000);
    REQUIRE(aprsStatus_stationCount() == 1);
    REQUIRE(aprsStatus_station(0, &s));
    REQUIRE(strcmp(s.call, "VA3ABC-7") == 0);
    REQUIRE(strcmp(s.text, "Audio") == 0);
}
