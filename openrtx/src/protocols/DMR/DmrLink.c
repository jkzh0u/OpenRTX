/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/DMR/DmrFec.h"
#include "protocols/DMR/DmrLink.h"

/* Burst layout (ETSI TS 102 361-1, 9.1): bit positions */
#define INFO1 0  /* 98 bits of information */
#define SLOT_TYPE1 98
#define SYNC 108 /* 48 bits: sync, or EMB + embedded signalling */
#define EMB1 108
#define EMBEDDED 116
#define EMB2 148
#define SLOT_TYPE2 156
#define INFO2 166

/* The CACH bit giving the timeslot of the next burst (TACT bit TC) */
#define CACH_TC 4

/* Link control CRC masks (ETSI TS 102 361-1, B.3.12) */
static const uint8_t maskHeader[3] = { 0x96, 0x96, 0x96 };
static const uint8_t maskTerminator[3] = { 0x99, 0x99, 0x99 };
#define MASK_CSBK 0xa5a5

/* Polarity: votes before changing it */
#define POLARITY_VOTES 3

static void emit(struct dmrLink *l, const struct dmrEvent *ev)
{
    if (l->onEvent != NULL)
        l->onEvent(l->user, ev);
}

static void callEvent(struct dmrLink *l, struct dmrLinkSlot *s,
                      enum dmrCallWhat what)
{
    struct dmrEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = DMR_EV_CALL;
    ev.u.call.what = what;
    ev.u.call.call = s->call;
    emit(l, &ev);
}

static void endCall(struct dmrLink *l, struct dmrLinkSlot *s)
{
    if (s->active) {
        s->active = false;
        callEvent(l, s, DMR_CALL_END);
    }
    s->voice = false;
    s->embCount = 0;
}

static void endAll(struct dmrLink *l)
{
    endCall(l, &l->slot[0]);
    endCall(l, &l->slot[1]);
}

static void resetState(struct dmrLink *l)
{
    memset(l->slot, 0, sizeof(l->slot));
    l->phaseKnown = false;
    l->phaseMisses = 0;
    l->lastSlot = 0;
}

void dmrLink_init(struct dmrLink *l, dmrLinkEventFn onEvent, void *user)
{
    memset(l, 0, sizeof(*l));
    dmrDemod_init(&l->demod, dmrLink_burst, l);
    l->onEvent = onEvent;
    l->user = user;
}

void dmrLink_reset(struct dmrLink *l)
{
    endAll(l);
    resetState(l);
    dmrDemod_reset(&l->demod);
    if (l->locked) {
        struct dmrEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = DMR_EV_LOCK;
        emit(l, &ev);
        l->locked = false;
    }
}

bool dmrLink_inverted(const struct dmrLink *l)
{
    return l->demod.inverted;
}

/** Bits as a number, first bit the MSB */
static uint32_t bitsValue(const uint8_t *bits, unsigned n)
{
    uint32_t v = 0;

    for (unsigned i = 0; i < n; i++)
        v = (v << 1) | (bits[i] & 1);
    return v;
}

/** The 20 bits of the slot type, with the sign bits flipped if asked */
static uint32_t slotTypeBits(const struct dmrBurst *b, bool flip)
{
    uint8_t bits[20];

    memcpy(bits, &b->bits[SLOT_TYPE1], 10);
    memcpy(&bits[10], &b->bits[SLOT_TYPE2], 10);
    if (flip) {
        /* The first bit of each dibit is its sign: SLOT_TYPE1 and
         * SLOT_TYPE2 start on a dibit */
        for (unsigned i = 0; i < 20; i += 2)
            bits[i] ^= 1;
    }
    return bitsValue(bits, 20);
}

/** Decode the 196 information bits of a data burst */
static bool decodeInfo(const struct dmrBurst *b, uint8_t out[12])
{
    uint8_t info[196];

    memcpy(info, &b->bits[INFO1], 98);
    memcpy(&info[98], &b->bits[INFO2], 98);
    return dmrFec_bptc19696Decode(info, out);
}

/** Fill a call from a link control (9 bytes); false if not a voice call */
static bool takeLc(struct dmrCall *c, const uint8_t lc[9])
{
    uint8_t flco = lc[0] & 0x3f;

    if (flco != DMR_FLCO_GROUP && flco != DMR_FLCO_PRIVATE)
        return false;

    c->ids = true;
    c->group = (flco == DMR_FLCO_GROUP);
    c->fid = lc[1];
    c->options = lc[2];
    c->dst = ((uint32_t)lc[3] << 16) | ((uint32_t)lc[4] << 8) | lc[5];
    c->src = ((uint32_t)lc[6] << 16) | ((uint32_t)lc[7] << 8) | lc[8];
    if (c->options & DMR_OPT_PRIVACY)
        c->encrypted = true;
    return true;
}

/** A link control names the slot's call: start it, or update it */
static void namedCall(struct dmrLink *l, struct dmrLinkSlot *s, uint8_t slot,
                      uint8_t cc, const uint8_t lc[9], bool late)
{
    struct dmrCall c;

    memset(&c, 0, sizeof(c));
    if (!takeLc(&c, lc))
        return;
    c.slot = slot;
    c.colourCode = cc;
    c.lateEntry = late;

    if (s->active) {
        struct dmrCall *o = &s->call;
        if (o->ids
            && (o->dst != c.dst || o->src != c.src || o->group != c.group)) {
            /* Another call: the terminator was missed */
            endCall(l, s);
        } else if (o->ids && o->colourCode == c.colourCode
                   && (o->encrypted || !c.encrypted)) {
            return; /* Nothing new */
        } else {
            c.encrypted = c.encrypted || o->encrypted;
            c.lateEntry = o->ids ? o->lateEntry : late;
            s->call = c;
            callEvent(l, s, DMR_CALL_UPDATE);
            return;
        }
    }

    s->call = c;
    s->active = true;
    s->lastActive = l->lastIndex;
    callEvent(l, s, DMR_CALL_START);
}

/** Voice heard on a slot whose call is not named yet */
static void unnamedCall(struct dmrLink *l, struct dmrLinkSlot *s, uint8_t slot,
                        uint8_t cc)
{
    if (s->active)
        return;
    memset(&s->call, 0, sizeof(s->call));
    s->call.slot = slot;
    s->call.colourCode = cc;
    s->active = true;
    callEvent(l, s, DMR_CALL_START);
}

/** Polarity evidence: a slot type decoded with or without flipping */
static void votePolarity(struct dmrLink *l, int vote)
{
    l->polarityVotes = (int8_t)(l->polarityVotes + vote);
    if (l->polarityVotes > POLARITY_VOTES)
        l->polarityVotes = POLARITY_VOTES;
    if (l->polarityVotes < -POLARITY_VOTES) {
        dmrDemod_setInverted(&l->demod, !l->demod.inverted);
        l->polarityVotes = 0;
        endAll(l);
        resetState(l);
    }
}

/**
 * The timeslot of a burst: from the CACH of a base station's downlink, kept
 * alternating when a CACH is misread; from the sync in direct mode.
 *
 * @return 0 or 1, or -1 for a single transmitter (timeslot unknown).
 */
static int slotOf(struct dmrLink *l, const struct dmrBurst *b)
{
    if (b->cachValid) {
        uint8_t tc = b->cach[CACH_TC];
        if (!l->phaseKnown) {
            l->slotPhase = (uint8_t)(tc ^ (b->index & 1));
            l->phaseKnown = true;
        }
        uint8_t expected = (uint8_t)((b->index + l->slotPhase) & 1);
        if (tc != expected) {
            if (++l->phaseMisses >= 3) {
                l->slotPhase ^= 1;
                l->phaseMisses = 0;
                expected = tc;
            }
        } else {
            l->phaseMisses = 0;
        }
        return expected;
    }

    if (b->sync == DMR_SYNC_TDMA1)
        l->lastSlot = 1;
    else if (b->sync == DMR_SYNC_TDMA2)
        l->lastSlot = 2;
    else if (b->sync == DMR_SYNC_MS)
        l->lastSlot = 0;

    return (l->lastSlot == 0) ? -1 : l->lastSlot - 1;
}

/** A burst decoded: the signal is DMR */
static void goodBurst(struct dmrLink *l)
{
    l->goodBursts++;
    if (!l->locked) {
        struct dmrEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = DMR_EV_LOCK;
        ev.u.lock.locked = true;
        ev.u.lock.sync = l->demod.family;
        l->locked = true;
        emit(l, &ev);
    }
}

static void dataBurst(struct dmrLink *l, const struct dmrBurst *b,
                      struct dmrLinkSlot *s, uint8_t slot)
{
    uint8_t st;
    int errs = dmrFec_golay2087Decode(slotTypeBits(b, false), &st);

    if (errs < 0) {
        l->badBursts++;
        return;
    }
    goodBurst(l);
    if (errs == 0)
        votePolarity(l, 1);

    uint8_t cc = st >> 4;
    uint8_t dt = st & 0x0f;
    uint8_t info[12];
    bool infoOk = false;

    if (dt == DMR_DT_VOICE_LC_HEADER || dt == DMR_DT_TERMINATOR_LC
        || dt == DMR_DT_CSBK)
        infoOk = decodeInfo(b, info);

    switch (dt) {
        case DMR_DT_VOICE_LC_HEADER:
        case DMR_DT_TERMINATOR_LC: {
            const uint8_t *mask = (dt == DMR_DT_VOICE_LC_HEADER) ?
                                      maskHeader :
                                      maskTerminator;
            for (int i = 0; i < 3; i++)
                info[9 + i] ^= mask[i];
            bool lcOk = dmrFec_rs129Check(info);
            if (dt == DMR_DT_VOICE_LC_HEADER) {
                if (lcOk)
                    namedCall(l, s, slot, cc, info, false);
                s->voice = false;
            } else {
                if (lcOk && s->active && !s->call.ids)
                    namedCall(l, s, slot, cc, info, false);
                endCall(l, s);
            }
            if (s->active)
                s->lastActive = b->index;
            return;
        }

        case DMR_DT_PI_HEADER:
            if (s->active && !s->call.encrypted) {
                s->call.encrypted = true;
                callEvent(l, s, DMR_CALL_UPDATE);
            }
            if (s->active)
                s->lastActive = b->index;
            break;

        default:
            break;
    }

    struct dmrEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DMR_EV_DATA;
    ev.u.data.slot = slot;
    ev.u.data.colourCode = cc;
    ev.u.data.dataType = dt;
    if (dt == DMR_DT_CSBK) {
        uint16_t crc = dmrFec_crcCcitt(info, 10) ^ MASK_CSBK;
        ev.u.data.crcOk = infoOk
                       && crc == (((uint16_t)info[10] << 8) | info[11]);
        ev.u.data.csbko = info[0] & 0x3f;
    }
    emit(l, &ev);

    /* An idle or data burst on a slot with a call: the call is over */
    if (s->active && dt != DMR_DT_PI_HEADER && !s->voice)
        endCall(l, s);
}

/** Voice burst B to F: the EMB and a fragment of the embedded LC */
static void embeddedBurst(struct dmrLink *l, const struct dmrBurst *b,
                          struct dmrLinkSlot *s, uint8_t slot)
{
    uint8_t embBits[16];
    uint8_t emb;

    memcpy(embBits, &b->bits[EMB1], 8);
    memcpy(&embBits[8], &b->bits[EMB2], 8);
    if (dmrFec_qr1676Decode((uint16_t)bitsValue(embBits, 16), &emb) < 0) {
        l->badBursts++;
        s->embCount = 0;
        return;
    }
    goodBurst(l);

    uint8_t cc = emb >> 3;
    bool pi = (emb >> 2) & 1;
    uint8_t lcss = emb & 3;

    /* Voice confirmed: a call joined late, not named yet */
    unnamedCall(l, s, slot, cc);
    s->lastActive = b->index;
    if (pi && s->active && !s->call.encrypted) {
        s->call.encrypted = true;
        callEvent(l, s, DMR_CALL_UPDATE);
    }

    /* LCSS: 1 first fragment, 3 continuation, 2 last, 0 single */
    const uint8_t *frag = &b->bits[EMBEDDED];
    if (lcss == 1) {
        memcpy(s->emb, frag, 32);
        s->embCount = 1;
    } else if (lcss == 3 && (s->embCount == 1 || s->embCount == 2)) {
        memcpy(&s->emb[32 * s->embCount], frag, 32);
        s->embCount++;
    } else if (lcss == 2 && s->embCount == 3) {
        uint8_t lc[9];
        memcpy(&s->emb[96], frag, 32);
        s->embCount = 0;
        if (dmrFec_embeddedLcDecode(s->emb, lc))
            namedCall(l, s, slot, cc, lc, true);
    } else {
        s->embCount = 0;
    }
}

void dmrLink_burst(void *link, const struct dmrBurst *b)
{
    struct dmrLink *l = (struct dmrLink *)link;

    /* Burst indices restart with each lock */
    if (b->index == 0)
        resetState(l);
    l->lastIndex = b->index;
    l->error = (l->error == 0) ? b->error : 0.8f * l->error + 0.2f * b->error;
    if (b->sync != DMR_SYNC_NONE) {
        l->dc = b->dc;
        l->level = b->level;
    }

    int si = slotOf(l, b);
    uint8_t slot = (si < 0) ? 0 : (uint8_t)(si + 1);
    struct dmrLinkSlot *s = &l->slot[(si < 0) ? 0 : si];
    /* Bursts between two of the same timeslot */
    uint32_t step = b->cachValid ? 2 : 1;

    if (b->sync != DMR_SYNC_NONE && !b->voice) {
        dataBurst(l, b, s, slot);
    } else if (b->sync != DMR_SYNC_NONE) {
        /* Voice burst A: would it be a data burst with the other polarity? */
        uint8_t st;
        if (dmrFec_golay2087Decode(slotTypeBits(b, true), &st) == 0)
            votePolarity(l, -1);
        s->voice = true;
        s->pos = 0;
        s->lastVoice = b->index;
        s->embCount = 0;
        if (s->active)
            s->lastActive = b->index;
    } else if (s->voice && b->index - s->lastVoice == step) {
        /* No sync where voice goes on: B to F, or an A whose sync was
         * lost */
        s->pos = (uint8_t)((s->pos + 1) % 6);
        s->lastVoice = b->index;
        if (s->pos != 0)
            embeddedBurst(l, b, s, slot);
    } else {
        l->badBursts++;
    }

    /* Calls with nothing heard for a while */
    for (int i = 0; i < 2; i++) {
        struct dmrLinkSlot *o = &l->slot[i];
        if (o->active && b->index - o->lastActive > DMR_LINK_CALL_TIMEOUT)
            endCall(l, o);
        if (o->voice && b->index - o->lastVoice > 2 * step)
            o->voice = false;
    }
}

void dmrLink_process(struct dmrLink *l, const int16_t *samples, unsigned n)
{
    dmrDemod_process(&l->demod, samples, n);

    if (l->locked && !dmrDemod_locked(&l->demod)) {
        struct dmrEvent ev;
        endAll(l);
        resetState(l);
        memset(&ev, 0, sizeof(ev));
        ev.type = DMR_EV_LOCK;
        l->locked = false;
        emit(l, &ev);
    }
}
