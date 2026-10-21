/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * sdr_smoke — Phase 1 task 1.10: 30 s hardware smoke test.
 *
 * Spawns a consumer task (Core 0, priority 12) that drains the IQ ring for
 * ~30 s, then reports the measured throughput and overrun counters and
 * asserts the M1 exit criteria:
 *   - effective_sps  > 900 000
 *   - overrun events == 0
 *
 * Call sdr_smoke_run() from app_main after the stream is confirmed started.
 */

/** Result of a completed smoke run. */
typedef struct {
    uint32_t     blocks;       /* IQ blocks consumed. */
    uint64_t     bytes;        /* IQ bytes consumed. */
    float        effective_sps;/* measured bytes/2/elapsed. */
    uint64_t     overrun_bytes;/* dropped bytes (from ring). */
    uint32_t     overrun_events;/* full-ring overwrites. */
    bool         pass;         /* true if M1 criteria met. */
} sdr_smoke_result_t;

/**
 * Run the 30 s smoke test, blocking until complete. Logs the verdict.
 * @return ESP_OK on completion, ESP_ERR_INVALID_STATE if ring/stream not ready.
 */
esp_err_t sdr_smoke_run(void);

#ifdef __cplusplus
}
#endif
