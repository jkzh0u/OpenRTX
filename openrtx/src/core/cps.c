/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/platform.h"
#include "core/cps.h"
#include <string.h>

const uint16_t ctcss_tone[CTCSS_FREQ_NUM] =
{
    670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035,
    1072, 1109, 1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567,
    1598, 1622, 1655, 1679, 1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928,
    1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541
};

const uint16_t dcs_code[DCS_CODE_NUM] =
{
    0023, 0025, 0026, 0031, 0032, 0036, 0043, 0047, 0051, 0053, 0054, 0065,
    0071, 0072, 0073, 0074, 0114, 0115, 0116, 0122, 0125, 0131, 0132, 0134,
    0143, 0145, 0152, 0155, 0156, 0162, 0165, 0172, 0174, 0205, 0212, 0223,
    0225, 0226, 0243, 0244, 0245, 0246, 0251, 0252, 0255, 0261, 0263, 0265,
    0266, 0271, 0274, 0306, 0311, 0315, 0325, 0331, 0332, 0343, 0346, 0351,
    0356, 0364, 0365, 0371, 0411, 0412, 0413, 0423, 0431, 0432, 0445, 0446,
    0452, 0454, 0455, 0462, 0464, 0465, 0466, 0503, 0506, 0516, 0523, 0526,
    0532, 0546, 0565, 0606, 0612, 0624, 0627, 0631, 0632, 0654, 0662, 0664,
    0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754
};

channel_t cps_getDefaultChannel()
{
    channel_t channel;

    memset(&channel, 0, sizeof(channel));

    #ifdef PLATFORM_MOD17
    channel.mode      = OPMODE_M17;
    #else
    channel.mode      = OPMODE_FM;
    #endif
    channel.bandwidth = BW_25;
    channel.power     = CPS_DEFAULT_POWER;
    channel.rx_only   = false;  // Enable tx by default

    // Set initial frequency based on supported bands
    const hwInfo_t* hwinfo  = platform_getHwInfo();
    if(hwinfo->uhf_band)
    {
        channel.rx_frequency = 430000000;
        channel.tx_frequency = 430000000;
    }
    else if(hwinfo->vhf_band)
    {
        channel.rx_frequency = 144000000;
        channel.tx_frequency = 144000000;
    }

    channel.fm.rxToneEn = 0; //disabled
    channel.fm.rxTone   = 0; //and no ctcss/dcs selected
    channel.fm.txToneEn = 0;
    channel.fm.txTone   = 0;
    return channel;
}
