/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Channel list image checks, on the image scripts/chlist.py builds from
 * tests/unit/tetra_ids_list.h (CHLIST_IMAGE).
 */

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core/chlist.h"

static std::vector<uint8_t> load()
{
    std::vector<uint8_t> image;
    FILE *f = std::fopen(CHLIST_IMAGE, "rb");
    REQUIRE(f != nullptr);
    int c;
    while ((c = std::fgetc(f)) != EOF)
        image.push_back((uint8_t)c);
    std::fclose(f);
    return image;
}

/** Recompute the CRC after changing the image */
static void reseal(std::vector<uint8_t> &image)
{
    uint32_t crc = chlist_crc32(image.data() + CHLIST_HEADER_SIZE,
                                image.size() - CHLIST_HEADER_SIZE);
    for (int i = 0; i < 4; i++)
        image[12 + i] = (uint8_t)(crc >> (8 * i));
}

TEST_CASE("Channel list CRC is CRC-32", "[chlist]")
{
    REQUIRE(chlist_crc32("123456789", 9) == 0xcbf43926u);
}

TEST_CASE("Channel list image is read", "[chlist]")
{
    std::vector<uint8_t> image = load();

    REQUIRE(chlist_open(image.data(), image.size()) == 0);
    REQUIRE(chlist_valid());
    REQUIRE(chlist_filterCount() == 3);
    REQUIRE(std::strcmp(chlist_filterName(0), "Subway") == 0);
    REQUIRE(chlist_filterName(3) == nullptr);

    struct chlistId id;
    REQUIRE(chlist_id(0, &id));
    REQUIRE(id.ssi != 0);
    REQUIRE(!chlist_id(chlist_idCount(), &id));

    struct chlistRange r;
    REQUIRE(chlist_rangeCount() > 0);
    REQUIRE(chlist_range(0, &r));
    REQUIRE(std::string(r.format).find("%u") != std::string::npos);

    // More room than the image is fine
    image.resize(image.size() + 100, 0xff);
    REQUIRE(chlist_open(image.data(), image.size()) == 0);
}

TEST_CASE("Damaged channel list images are refused", "[chlist]")
{
    const std::vector<uint8_t> good = load();
    std::vector<uint8_t> image;

    // Erased flash, or too small
    std::vector<uint8_t> erased(4096, 0xff);
    REQUIRE(chlist_open(erased.data(), erased.size()) == -1);
    REQUIRE(chlist_open(good.data(), 16) == -1);
    REQUIRE(chlist_open(nullptr, 0) == -1);

    // Another version; version 1, without priorities, is still read
    image = good;
    image[4] = CHLIST_VERSION + 1;
    REQUIRE(chlist_open(image.data(), image.size()) == -2);
    image[4] = 0;
    REQUIRE(chlist_open(image.data(), image.size()) == -2);
    image[4] = 1;
    REQUIRE(chlist_open(image.data(), image.size()) == 0);

    // Cut short
    REQUIRE(chlist_open(good.data(), good.size() - 1) == -3);

    // A byte changed
    image = good;
    image[CHLIST_HEADER_SIZE] ^= 1;
    REQUIRE(chlist_open(image.data(), image.size()) == -4);

    // A name format that is not one %u, CRC fixed
    image = good;
    static const char pct[] = "%u";
    auto at = std::search(image.begin(), image.end(), pct, pct + 2);
    REQUIRE(at != image.end());
    at[1] = 's';
    reseal(image);
    REQUIRE(chlist_open(image.data(), image.size()) == -5);

    // A name past the strings, CRC fixed
    image = good;
    image[CHLIST_HEADER_SIZE + 8] = 0xff; // first channel's name
    image[CHLIST_HEADER_SIZE + 9] = 0xff;
    reseal(image);
    REQUIRE(chlist_open(image.data(), image.size()) == -5);

    // After a refusal the list is empty
    REQUIRE(!chlist_valid());
    REQUIRE(chlist_filterCount() == 0);
    REQUIRE(chlist_idCount() == 0);
    REQUIRE(chlist_channelCount() == 0);
}
