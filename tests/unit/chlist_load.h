/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * For tests built with CONFIG_CHLIST: opens the channel list image named by
 * CHLIST_IMAGE, built by scripts/chlist.py from the test's list, before the
 * tests run. The same tests then check the image against the list.
 */

#ifndef CHLIST_LOAD_H
#define CHLIST_LOAD_H

#ifdef CONFIG_CHLIST

#include <cstdio>
#include <cstdlib>
#include <vector>
#include "core/chlist.h"

#ifndef CHLIST_IMAGE
#error "CHLIST_IMAGE must name the channel list image"
#endif

namespace
{

struct ChlistLoader {
    std::vector<uint8_t> image;

    ChlistLoader()
    {
        FILE *f = std::fopen(CHLIST_IMAGE, "rb");
        if (f == nullptr) {
            std::fprintf(stderr, "cannot open %s\n", CHLIST_IMAGE);
            std::exit(1);
        }
        int c;
        while ((c = std::fgetc(f)) != EOF)
            image.push_back((uint8_t)c);
        std::fclose(f);

        int ret = chlist_open(image.data(), image.size());
        if (ret != 0) {
            std::fprintf(stderr, "%s: chlist_open() %d\n", CHLIST_IMAGE, ret);
            std::exit(1);
        }
    }
};

ChlistLoader chlistLoader;

} // namespace

#endif

#endif /* CHLIST_LOAD_H */
