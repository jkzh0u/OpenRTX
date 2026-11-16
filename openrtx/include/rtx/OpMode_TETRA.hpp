/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPMODE_TETRA_H
#define OPMODE_TETRA_H

#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "OpMode.hpp"

/**
 * TETRA TMO downlink receive mode: demodulates the radio's flat FM
 * discriminator output (pi/4-DQPSK by integrate-and-dump) and decodes it
 * with libtetra. Receive only: PTT does nothing in this mode.
 *
 * Needs a 48 kHz RX audio input and a radio driver that sets up a wide,
 * flat receive path for OPMODE_TETRA.
 *
 * With CONFIG_TETRA_FOLLOW, follows calls to the traffic carriers of the
 * cell when libtetra asks to retune (see TetraFollow.h): it retunes by
 * changing the receive frequency in the rtx status, keeping the receiver's
 * slot timing. The radio driver should retune quickly in that case, well
 * within a timeslot (14.17 ms).
 */
class OpMode_TETRA : public OpMode
{
public:
    OpMode_TETRA();
    ~OpMode_TETRA();

    virtual void enable() override;
    virtual void disable() override;
    virtual void update(rtxStatus_t *const status, const bool newCfg) override;

    virtual opmode getID() override
    {
        return OPMODE_TETRA;
    }

    virtual bool rxSquelchOpen() override
    {
        return false;
    }

private:
    void reset();
    void resync(bool overrun);
    void startSampling();
    void stopSampling();

    bool startRx;
    unsigned dataErrors; // Consecutive failed reads of the sample stream
    uint32_t demodJumps; // Demodulator slips and re-acquisitions seen
    uint32_t homeFreq;   // Configured frequency (main carrier), Hz
    pathId basebandPath;
    streamId basebandId;
};

#endif /* OPMODE_TETRA_H */
