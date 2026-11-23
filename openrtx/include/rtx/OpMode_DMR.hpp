/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPMODE_DMR_H
#define OPMODE_DMR_H

#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "OpMode.hpp"

/**
 * DMR receive mode: demodulates the radio's flat FM discriminator output
 * (4FSK) and decodes who calls whom on each timeslot (DmrLink.h), for the
 * UI (DmrStatus.h). Receive only, and silent: DMR speech is AMBE+2, a
 * proprietary codec. PTT does nothing in this mode.
 *
 * Needs a 48 kHz RX audio input and a radio driver that sets up a flat
 * receive path for OPMODE_DMR, as for M17.
 */
class OpMode_DMR : public OpMode
{
public:
    OpMode_DMR();
    ~OpMode_DMR();

    virtual void enable() override;
    virtual void disable() override;
    virtual void update(rtxStatus_t *const status, const bool newCfg) override;

    virtual opmode getID() override
    {
        return OPMODE_DMR;
    }

    virtual bool rxSquelchOpen() override
    {
        return false;
    }

private:
    void reset();
    void startSampling();
    void stopSampling();

    bool startRx;
    unsigned dataErrors; // Consecutive failed reads of the sample stream
    size_t settle;       // Samples still to drop after a start
    uint32_t freq;       // Frequency received, Hz
    pathId basebandPath;
    streamId basebandId;
};

#endif /* OPMODE_DMR_H */
