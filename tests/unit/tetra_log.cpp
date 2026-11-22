/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>
#include "protocols/TETRA/TetraLog.h"

/* Everything the sender would send now */
static std::string drain(size_t chunk = 64)
{
    std::string out;
    char buf[256];
    size_t n;

    while ((n = tetraLog_next(buf, chunk)) > 0)
        out.append(buf, n);
    return out;
}

TEST_CASE("TETRA log lines carry the time", "[tetra][log]")
{
    tetraLog_init(nullptr, 0);
    REQUIRE(drain().empty());

    tetraLog_printf(12345, "sync %s", "locked");
    tetraLog_printf(3600001, "call %u", 7u);
    REQUIRE(drain()
            == "    12.345  sync locked\n"
               "  3600.001  call 7\n");
    REQUIRE(drain().empty());
}

TEST_CASE("TETRA log truncates long lines", "[tetra][log]")
{
    tetraLog_init(nullptr, 0);
    std::string longText(300, 'x');

    tetraLog_printf(0, "%s", longText.c_str());
    std::string out = drain();
    REQUIRE(out.size() == TETRA_LOG_LINE_MAX - 1);
    REQUIRE(out.back() == '\n');
}

TEST_CASE("TETRA log drops lines the line cannot keep up with", "[tetra][log]")
{
    tetraLog_init(nullptr, 0);
    std::string text(87, 'a'); // 100 bytes a line with the time

    unsigned lines = TETRA_LOG_LIVE_SIZE / 100 + 10;
    for (unsigned i = 0; i < lines; i++)
        tetraLog_printf(0, "%s", text.c_str());

    REQUIRE(tetraLog_dropped() == 10);
    std::string out = drain();
    std::string notice = "--- 10 lines not sent: line too slow ---\n";
    REQUIRE(out.size() == (lines - 10) * 100 + notice.size());
    REQUIRE(out.substr(out.size() - notice.size()) == notice);

    // Room again
    tetraLog_printf(1000, "after");
    REQUIRE(drain() == "     1.000  after\n");
}

TEST_CASE("TETRA log keeps a history to send on request", "[tetra][log]")
{
    static char history[256];
    tetraLog_init(history, sizeof(history));

    SECTION("nothing asked: only the live lines")
    {
        tetraLog_printf(1000, "one");
        REQUIRE(drain() == "     1.000  one\n");
        REQUIRE(drain().empty());
    }

    SECTION("all of it, after the live lines")
    {
        tetraLog_printf(1000, "one");
        tetraLog_printf(2000, "two");
        REQUIRE(drain() == "     1.000  one\n     2.000  two\n");

        tetraLog_requestHistory();
        tetraLog_printf(3000, "three");
        REQUIRE(drain(7)
                == "     3.000  three\n"
                   "--- stored log, 50 bytes ---\n"
                   "     1.000  one\n"
                   "     2.000  two\n"
                   "     3.000  three\n"
                   "--- end of stored log ---\n");
    }

    SECTION("only the newest lines once full, from a line start")
    {
        for (unsigned i = 0; i < 40; i++)
            tetraLog_printf(i * 1000, "line %02u", i);
        drain();

        tetraLog_requestHistory();
        std::string out = drain();
        REQUIRE(out.rfind("--- stored log, 256 bytes ---\n", 0) == 0);
        // 20 byte lines: the newest 12 whole ones, not the cut 13th
        REQUIRE(out.substr(30) == [] {
            std::string lines;
            char line[32];
            for (unsigned i = 28; i < 40; i++) {
                snprintf(line, sizeof(line), "%6u.000  line %02u\n", i, i);
                lines += line;
            }
            return lines + "--- end of stored log ---\n";
        }());
    }

    SECTION("asked without a history")
    {
        tetraLog_init(nullptr, 0);
        tetraLog_requestHistory();
        REQUIRE(drain() == "--- no stored log ---\n");
    }
}
