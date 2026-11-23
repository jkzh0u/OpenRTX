/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include "protocols/DMR/DmrFec.h"
#include "protocols/DMR/DmrLink.h"
#include "protocols/DMR/DmrStatus.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Tables and frames of MMDVMHost (G4KLX), an independent implementation:
 * the first parities of its Golay(20,8) and QR(16,7,6) encoding tables, and
 * a complete idle data burst.
 */
static const uint16_t golayTable[12] = {
    0x0000, 0xB08E, 0xE093, 0x501D, 0x70A9, 0xC027,
    0x903A, 0x20B4, 0x60DC, 0xD052, 0x804F, 0x30C1,
};
static const uint16_t qrTable[12] = {
    0x0000, 0x0273, 0x04E5, 0x0696, 0x09C9, 0x0BBA,
    0x0D2C, 0x0F5F, 0x11E2, 0x1391, 0x1507, 0x1774,
};
static const uint8_t idleBurst[33] = {
    0x53, 0xC2, 0x5E, 0xAB, 0xA8, 0x67, 0x1D, 0xC7, 0x38, 0x3B, 0xD9,
    0x36, 0x00, 0x0D, 0xFF, 0x57, 0xD7, 0x5D, 0xF5, 0xD0, 0x03, 0xF6,
    0xE4, 0x65, 0x17, 0x1B, 0x48, 0xCA, 0x6D, 0x4F, 0xC6, 0x10, 0xB4,
};

static void toBits(const uint8_t *bytes, unsigned n, uint8_t *bits)
{
    for (unsigned i = 0; i < n * 8; i++)
        bits[i] = (bytes[i / 8] >> (7 - i % 8)) & 1;
}

TEST_CASE("DMR Golay(20,8) matches MMDVMHost and corrects 3 errors",
          "[dmr][fec]")
{
    for (unsigned d = 0; d < 12; d++) {
        uint32_t cw = dmrFec_golay2087Encode((uint8_t)d);
        /* MMDVMHost keeps the 12 parity bits left aligned, bytes swapped */
        uint16_t p = (uint16_t)((cw & 0xfff) << 4);
        REQUIRE((uint16_t)((p >> 8) | (p << 8)) == golayTable[d]);
    }

    std::mt19937 rng(1);
    for (int t = 0; t < 2000; t++) {
        uint8_t d = rng() & 0xff, out;
        uint32_t cw = dmrFec_golay2087Encode(d);
        int nerr = t % 4;
        for (int e = 0; e < nerr; e++)
            cw ^= 1u << ((rng() % 20));
        int r = dmrFec_golay2087Decode(cw, &out);
        REQUIRE(r >= 0);
        REQUIRE(r <= nerr);
        REQUIRE(out == d);
    }
}

TEST_CASE("DMR QR(16,7,6) matches MMDVMHost and corrects 2 errors",
          "[dmr][fec]")
{
    for (unsigned d = 0; d < 12; d++)
        REQUIRE(dmrFec_qr1676Encode((uint8_t)d) == qrTable[d]);

    for (unsigned d = 0; d < 128; d++) {
        for (unsigned a = 0; a < 16; a++) {
            for (unsigned b = a; b < 16; b++) {
                uint8_t out;
                uint16_t cw = dmrFec_qr1676Encode((uint8_t)d);
                cw ^= (uint16_t)(1u << a);
                if (b != a)
                    cw ^= (uint16_t)(1u << b);
                REQUIRE(dmrFec_qr1676Decode(cw, &out) >= 0);
                REQUIRE(out == d);
            }
        }
    }
}

TEST_CASE("DMR idle burst of MMDVMHost decodes", "[dmr][fec]")
{
    uint8_t bits[264], st, info[196], out[12];

    toBits(idleBurst, 33, bits);

    uint32_t code = 0;
    for (int i = 0; i < 10; i++)
        code = (code << 1) | bits[98 + i];
    for (int i = 0; i < 10; i++)
        code = (code << 1) | bits[156 + i];
    /* MMDVMHost fills the slot type in when sending: zero here */
    REQUIRE(dmrFec_golay2087Decode(code, &st) == 0);
    REQUIRE(st == 0);

    /* Base station data sync */
    uint64_t sync = 0;
    for (int i = 0; i < 48; i++)
        sync = (sync << 1) | bits[108 + i];
    REQUIRE(sync == 0xDFF57D75DF5Dull);

    memcpy(info, bits, 98);
    memcpy(&info[98], &bits[166], 98);
    REQUIRE(dmrFec_bptc19696Decode(info, out));
}

TEST_CASE("DMR BPTC(196,96) round trip and error correction", "[dmr][fec]")
{
    std::mt19937 rng(2);

    for (int t = 0; t < 200; t++) {
        uint8_t in[12], out[12], bits[196];
        for (auto &b : in)
            b = rng() & 0xff;
        dmrFec_bptc19696Encode(in, bits);
        /* Errors in different rows and columns of the matrix */
        int nerr = t % 4;
        for (int e = 0; e < nerr; e++)
            bits[(e * 53 + t) % 196] ^= 1;
        REQUIRE(dmrFec_bptc19696Decode(bits, out));
        REQUIRE(memcmp(in, out, 12) == 0);
    }
}

TEST_CASE("DMR RS(12,9) and embedded link control", "[dmr][fec]")
{
    uint8_t lc[12] = { 0x00, 0x00, 0x00, 0x00, 0x0b, 0xcc, 0x2e, 0x1c, 0xb2 };
    dmrFec_rs129Parity(lc, &lc[9]);
    REQUIRE(dmrFec_rs129Check(lc));
    lc[4] ^= 0x10;
    REQUIRE_FALSE(dmrFec_rs129Check(lc));
    lc[4] ^= 0x10;

    uint8_t raw[128], back[9];
    dmrFec_embeddedLcEncode(lc, raw);
    REQUIRE(dmrFec_embeddedLcDecode(raw, back));
    REQUIRE(memcmp(lc, back, 9) == 0);
    raw[77] ^= 1; /* One error is corrected */
    REQUIRE(dmrFec_embeddedLcDecode(raw, back));
    REQUIRE(memcmp(lc, back, 9) == 0);
}

/*
 * A DMR transmitter: bursts as bits, 4FSK with a root raised cosine filter
 * at a symbol rate a little off, as a discriminator would output it.
 */
namespace
{

const uint64_t BS_VOICE = 0x755FD7DF75F7ull;
const uint64_t MS_VOICE = 0x7F7D5DD57DFDull;
const uint64_t DATA_FLIP = 0xAAAAAAAAAAAAull;

struct Lc {
    bool group;
    uint32_t dst, src;
    uint8_t options;
};

void lcBytes(const Lc &c, uint8_t lc[12])
{
    memset(lc, 0, 12);
    lc[0] = c.group ? DMR_FLCO_GROUP : DMR_FLCO_PRIVATE;
    lc[2] = c.options;
    lc[3] = c.dst >> 16;
    lc[4] = c.dst >> 8;
    lc[5] = c.dst;
    lc[6] = c.src >> 16;
    lc[7] = c.src >> 8;
    lc[8] = c.src;
}

struct Tx {
    std::vector<uint8_t> bits; /* Dibits as bit pairs */
    std::mt19937 rng{ 7 };

    void put(uint64_t v, int n)
    {
        for (int i = n - 1; i >= 0; i--)
            bits.push_back((v >> i) & 1);
    }

    void random(int n)
    {
        for (int i = 0; i < n; i++)
            bits.push_back(rng() & 1);
    }

    void cach(int slot)
    {
        /* TACT bit TC at bit 4, the rest random */
        for (int i = 0; i < 24; i++)
            bits.push_back((i == 4) ? (uint8_t)(slot - 1) : (rng() & 1));
    }

    void slotType(uint8_t cc, uint8_t dt, int half)
    {
        uint32_t cw = dmrFec_golay2087Encode((uint8_t)(cc << 4 | dt));
        put(half == 0 ? (cw >> 10) : (cw & 0x3ff), 10);
    }

    void data(uint8_t cc, uint8_t dt, const uint8_t info[12], uint64_t sync)
    {
        uint8_t b[196];
        dmrFec_bptc19696Encode(info, b);
        bits.insert(bits.end(), b, b + 98);
        slotType(cc, dt, 0);
        put(sync ^ DATA_FLIP, 48);
        slotType(cc, dt, 1);
        bits.insert(bits.end(), b + 98, b + 196);
    }

    void lcBurst(uint8_t cc, uint8_t dt, const Lc &c, uint64_t sync)
    {
        uint8_t lc[12];
        lcBytes(c, lc);
        dmrFec_rs129Parity(lc, &lc[9]);
        uint8_t m = (dt == DMR_DT_VOICE_LC_HEADER) ? 0x96 : 0x99;
        lc[9] ^= m;
        lc[10] ^= m;
        lc[11] ^= m;
        data(cc, dt, lc, sync);
    }

    void idle(uint8_t cc, uint64_t sync)
    {
        uint8_t info[12];
        for (auto &b : info)
            b = rng() & 0xff;
        data(cc, DMR_DT_IDLE, info, sync);
    }

    /* Voice burst n (0 = A) of a superframe */
    void voice(uint8_t cc, const Lc &c, int n, uint64_t sync)
    {
        random(108);
        if (n == 0) {
            put(sync, 48);
        } else {
            static const uint8_t lcss[6] = { 0, 1, 3, 3, 2, 0 };
            uint8_t lc[12], raw[128];
            lcBytes(c, lc);
            dmrFec_embeddedLcEncode(lc, raw);
            uint16_t emb =
                dmrFec_qr1676Encode((uint8_t)(cc << 3 | (lcss[n] & 3)));
            put(emb >> 8, 8);
            if (n >= 1 && n <= 4)
                bits.insert(bits.end(), raw + 32 * (n - 1), raw + 32 * n);
            else
                put(0, 32);
            put(emb & 0xff, 8);
        }
        random(108);
    }
};

double rrcPulse(double t)
{
    const double a = 0.2;
    if (std::fabs(t) < 1e-9)
        return 1.0 - a + 4.0 * a / M_PI;
    if (std::fabs(std::fabs(t) - 1.0 / (4.0 * a)) < 1e-9)
        return a / std::sqrt(2.0)
             * ((1.0 + 2.0 / M_PI) * std::sin(M_PI / (4.0 * a))
                + (1.0 - 2.0 / M_PI) * std::cos(M_PI / (4.0 * a)));
    return (std::sin(M_PI * t * (1.0 - a))
            + 4.0 * a * t * std::cos(M_PI * t * (1.0 + a)))
         / (M_PI * t * (1.0 - (4.0 * a * t) * (4.0 * a * t)));
}

/*
 * The discriminator output: lead samples of noise first, then the bits;
 * symbol rate off by ppm; deviation of an outer symbol in input units.
 */
std::vector<int16_t> modulate(const std::vector<uint8_t> &bits, int lead,
                              double ppm, double outer, double dc, double noise,
                              bool invert, unsigned seed = 3)
{
    std::vector<double> sym;
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        double v = bits[i + 1] ? 3.0 : 1.0;
        sym.push_back(bits[i] ? -v : v);
    }

    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);
    double rate = 4800.0 * (1.0 + ppm * 1e-6) / 48000.0;
    size_t total = (size_t)lead + (size_t)((double)sym.size() / rate) + 480;
    std::vector<int16_t> out(total);

    for (size_t n = 0; n < total; n++) {
        double v = 0;
        if ((long)n >= lead) {
            double t = (double)(n - lead) * rate;
            long k0 = (long)std::floor(t);
            for (long k = k0 - 5; k <= k0 + 5; k++) {
                if (k < 0 || k >= (long)sym.size())
                    continue;
                v += sym[k] * rrcPulse(t - (double)k);
            }
        }
        v = v * outer / 3.0 * (invert ? -1 : 1) + dc + noise * gauss(rng);
        out[n] = (int16_t)std::max(-32768.0, std::min(32767.0, v));
    }

    return out;
}

/* A base station downlink: a call on timeslot 1, idle on timeslot 2 */
std::vector<uint8_t> bsCall(const Lc &c, uint8_t cc, int superframes,
                            bool headers, bool terminator)
{
    Tx tx;
    auto pair = [&](auto slot1) {
        tx.cach(1);
        slot1();
        tx.cach(2);
        tx.idle(cc, BS_VOICE);
    };

    for (int i = 0; i < 4; i++)
        pair([&] { tx.idle(cc, BS_VOICE); });
    if (headers) {
        for (int i = 0; i < 3; i++)
            pair([&] { tx.lcBurst(cc, DMR_DT_VOICE_LC_HEADER, c, BS_VOICE); });
    }
    for (int f = 0; f < superframes; f++) {
        for (int n = 0; n < 6; n++)
            pair([&] { tx.voice(cc, c, n, BS_VOICE); });
    }
    if (terminator) {
        for (int i = 0; i < 2; i++)
            pair([&] { tx.lcBurst(cc, DMR_DT_TERMINATOR_LC, c, BS_VOICE); });
    }
    for (int i = 0; i < 6; i++)
        pair([&] { tx.idle(cc, BS_VOICE); });

    return tx.bits;
}

struct Rx {
    struct dmrLink link;
    std::vector<dmrEvent> events;

    Rx()
    {
        dmrLink_init(
            &link,
            [](void *u, const dmrEvent *ev) {
                static_cast<Rx *>(u)->events.push_back(*ev);
                if (getenv("DMR_DEBUG")) {
                    if (ev->type == DMR_EV_CALL) {
                        const dmrCall &c = ev->u.call.call;
                        printf("call %d slot %d cc %d ids %d %u -> %u enc %d "
                               "late %d\n",
                               ev->u.call.what, c.slot, c.colourCode, c.ids,
                               c.src, c.dst, c.encrypted, c.lateEntry);
                    } else if (ev->type == DMR_EV_LOCK) {
                        printf("lock %d sync %d\n", ev->u.lock.locked,
                               ev->u.lock.sync);
                    } else {
                        printf("data slot %d cc %d dt %d\n", ev->u.data.slot,
                               ev->u.data.colourCode, ev->u.data.dataType);
                    }
                }
            },
            this);
    }

    void run(const std::vector<int16_t> &s)
    {
        /* In blocks, as the radio delivers them */
        for (size_t i = 0; i < s.size(); i += 1440)
            dmrLink_process(&link, &s[i],
                            (unsigned)std::min<size_t>(1440, s.size() - i));
    }

    std::vector<dmrEvent> calls(enum dmrCallWhat what) const
    {
        std::vector<dmrEvent> r;
        for (const auto &e : events) {
            if (e.type == DMR_EV_CALL && e.u.call.what == what)
                r.push_back(e);
        }
        return r;
    }

    /* Last state of the call that ended */
    dmrCall ended() const
    {
        auto e = calls(DMR_CALL_END);
        REQUIRE(e.size() == 1);
        return e[0].u.call.call;
    }
};

} // namespace

TEST_CASE("DMR base station group call", "[dmr][rx]")
{
    Lc c{ true, 3020, 3021234, 0 };
    Rx rx;

    rx.run(
        modulate(bsCall(c, 1, 3, true, true), 1234, 40, 6000, 500, 300, false));

    dmrCall e = rx.ended();
    REQUIRE(e.slot == 1);
    REQUIRE(e.colourCode == 1);
    REQUIRE(e.ids);
    REQUIRE(e.group);
    REQUIRE(e.dst == 3020);
    REQUIRE(e.src == 3021234);
    REQUIRE_FALSE(e.encrypted);
    REQUIRE_FALSE(e.lateEntry);
    REQUIRE(rx.calls(DMR_CALL_START).size() == 1);
    REQUIRE_FALSE(dmrLink_inverted(&rx.link));
}

TEST_CASE("DMR late entry from the embedded link control", "[dmr][rx]")
{
    Lc c{ false, 1234567, 7654321, DMR_OPT_PRIVACY };
    Rx rx;

    rx.run(modulate(bsCall(c, 7, 4, false, false), 77, -30, 5000, -800, 250,
                    false));

    auto start = rx.calls(DMR_CALL_START);
    REQUIRE(start.size() == 1);
    REQUIRE_FALSE(start[0].u.call.call.ids);

    dmrCall e = rx.ended();
    REQUIRE(e.ids);
    REQUIRE(e.lateEntry);
    REQUIRE_FALSE(e.group);
    REQUIRE(e.dst == 1234567);
    REQUIRE(e.src == 7654321);
    REQUIRE(e.encrypted);
    REQUIRE(e.colourCode == 7);
}

TEST_CASE("DMR inverted discriminator is detected", "[dmr][rx]")
{
    Lc c{ true, 91, 2345678, 0 };
    Rx rx;

    rx.run(modulate(bsCall(c, 3, 3, true, true), 500, 0, 7000, 0, 300, true));

    REQUIRE(dmrLink_inverted(&rx.link));
    dmrCall e = rx.ended();
    REQUIRE(e.ids);
    REQUIRE(e.dst == 91);
    REQUIRE(e.src == 2345678);
}

TEST_CASE("DMR simplex call from a mobile", "[dmr][rx]")
{
    Lc c{ true, 99, 3027001, 0 };
    Tx tx;

    /* One timeslot of two: a burst every 60 ms (288 symbols), inner
     * symbols in between */
    auto gap = [&] {
        for (int i = 0; i < 144; i++) {
            tx.bits.push_back(i & 1);
            tx.bits.push_back(0);
        }
    };
    for (int i = 0; i < 3; i++) {
        tx.lcBurst(1, DMR_DT_VOICE_LC_HEADER, c, MS_VOICE);
        tx.random(24);
        gap();
    }
    for (int f = 0; f < 3; f++) {
        for (int n = 0; n < 6; n++) {
            tx.voice(1, c, n, MS_VOICE);
            tx.random(24);
            gap();
        }
    }
    tx.lcBurst(1, DMR_DT_TERMINATOR_LC, c, MS_VOICE);
    tx.random(24);

    Rx rx;
    rx.run(modulate(tx.bits, 3000, 20, 6000, 300, 300, false));

    dmrCall e = rx.ended();
    REQUIRE(e.slot == 0);
    REQUIRE(e.ids);
    REQUIRE(e.dst == 99);
    REQUIRE(e.src == 3027001);
}

TEST_CASE("DMR noise alone does not lock", "[dmr][rx]")
{
    std::vector<uint8_t> none;
    Rx rx;

    rx.run(modulate(none, 48000 * 5, 0, 6000, 0, 3000, false));

    for (const auto &e : rx.events)
        REQUIRE(e.type != DMR_EV_CALL);
}

TEST_CASE("DMR status filters and recent calls", "[dmr][status]")
{
    dmrEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DMR_EV_CALL;
    dmrCall &c = ev.u.call.call;
    c.slot = 2;
    c.colourCode = 1;
    c.ids = true;
    c.group = true;
    c.dst = 3020;
    c.src = 3021234;

    dmrStatus_setFilter(DMR_SLOTS_BOTH, 0);
    dmrStatus_reset(446000000);
    ev.u.call.what = DMR_CALL_START;
    REQUIRE(dmrStatus_onEvent(&ev, 1000));

    dmrStatus st;
    dmr_getStatus(&st);
    const dmrStatusCall *shown = dmr_shownCall(&st, 1500);
    REQUIRE(shown != nullptr);
    REQUIRE(shown->call.dst == 3020);
    REQUIRE(shown->active);

    ev.u.call.what = DMR_CALL_END;
    dmrStatus_onEvent(&ev, 5000);
    dmr_getStatus(&st);
    REQUIRE(st.nRecent == 1);
    REQUIRE(dmr_shownCall(&st, 6000) == &st.recent[0]);
    REQUIRE(dmr_shownCall(&st, 5000 + DMR_STATUS_HANG_MS) == nullptr);

    /* The same call again replaces its entry */
    ev.u.call.what = DMR_CALL_START;
    dmrStatus_onEvent(&ev, 9000);
    ev.u.call.what = DMR_CALL_END;
    dmrStatus_onEvent(&ev, 9500);
    dmr_getStatus(&st);
    REQUIRE(st.nRecent == 1);

    /* Filters: timeslot 1 only, then colour code 2 only */
    dmrStatus_setFilter(DMR_SLOTS_1, 0);
    ev.u.call.what = DMR_CALL_START;
    REQUIRE_FALSE(dmrStatus_onEvent(&ev, 10000));
    c.slot = 1;
    REQUIRE(dmrStatus_onEvent(&ev, 10000));
    dmrStatus_setFilter(DMR_SLOTS_BOTH, 3);
    REQUIRE_FALSE(dmrStatus_onEvent(&ev, 10000));
    c.colourCode = 2;
    REQUIRE(dmrStatus_onEvent(&ev, 10000));
    /* A single transmitter has no timeslot */
    dmrStatus_setFilter(DMR_SLOTS_2, 0);
    c.slot = 0;
    REQUIRE(dmrStatus_wanted(&c));
    dmrStatus_setFilter(DMR_SLOTS_BOTH, 0);
}
