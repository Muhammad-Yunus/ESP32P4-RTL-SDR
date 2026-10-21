/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * sdr_dsp — Phase 2.1: the DSP consumer task.
 *
 * Spawns the sdr_dsp task (Core 0, prio 12, 16 K stack per ARCHITECTURE
 * Section 2). The task drains the IQ ring and feeds the spectrum processor
 * (task 2.1); waterfall (2.2) and FM demod (Phase 4) will share this task
 * later. Emits a 1 Hz metric line.
 */

/**
 * Start the DSP task. Idempotent: a second call returns ESP_ERR_INVALID_STATE.
 * Requires iq_ring + spectrum to be initialised (call spectrum_init first).
 * @return ESP_OK on success.
 */
esp_err_t sdr_dsp_start(void);

/**
 * Stop the DSP task and free it. Idempotent.
 */
esp_err_t sdr_dsp_stop(void);

/**
 * Number of IQ blocks consumed since start. For metrics / UI.
 */
uint64_t sdr_dsp_blocks_consumed(void);

#ifdef __cplusplus
}
#endif
