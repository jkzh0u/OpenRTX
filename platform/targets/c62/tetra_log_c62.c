/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * TETRA log (openrtx/include/protocols/TETRA/TetraLog.h) on the C62: the
 * history ring in PSRAM, and a sender thread writing the lines to the
 * console UART, the TX line of the Kenwood connector, at the console baud
 * rate (115200).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include "protocols/TETRA/TetraLog.h"

#ifndef C62_TETRA_LOG_HISTORY
#define C62_TETRA_LOG_HISTORY (256 * 1024)
#endif

#define CONSOLE_NODE DT_CHOSEN(zephyr_console)

/*
 * 32 bytes fill the UART FIFO and take 2.8 ms at 115200 baud: write that
 * much, then sleep, so the thread does not spin waiting for the FIFO.
 */
#define CHUNK 32
#define CHUNK_SLEEP_MS 3
#define IDLE_SLEEP_MS 20

#define SENDER_STACK_SIZE 1024

static char history[C62_TETRA_LOG_HISTORY]
    __attribute__((section(".psram_section")));

static struct k_thread sender;
K_THREAD_STACK_DEFINE(sender_stack, SENDER_STACK_SIZE);

static void sender_main(void *p1, void *p2, void *p3)
{
    const struct device *uart = DEVICE_DT_GET(CONSOLE_NODE);
    char buf[CHUNK];

    (void)p1;
    (void)p2;
    (void)p3;

    for (;;) {
        size_t n = tetraLog_next(buf, sizeof(buf));
        if (n == 0) {
            k_msleep(IDLE_SLEEP_MS);
            continue;
        }
        for (size_t i = 0; i < n; i++)
            uart_poll_out(uart, (unsigned char)buf[i]);
        k_msleep(CHUNK_SLEEP_MS);
    }
}

/**
 * Start the TETRA log: call once at boot, before the receiver runs.
 */
void c62_tetraLogInit(void)
{
    tetraLog_init(history, sizeof(history));
    tetraLog_printf(k_uptime_get(), "TETRA log, %u kB kept, side key + 0 "
                                    "sends them",
                    (unsigned)(sizeof(history) / 1024));

    /* Lowest priority: the log only uses time nothing else wants */
    k_thread_create(&sender, sender_stack, K_THREAD_STACK_SIZEOF(sender_stack),
                    sender_main, NULL, NULL, NULL,
                    K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&sender, "tetralog");
}
