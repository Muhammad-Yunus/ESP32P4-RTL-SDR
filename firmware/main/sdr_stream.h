/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdr_control.h"
#include "iq_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * sdr_stream — Phase 1.8 / 1.9 streaming layer.
 *
 * Ties the sdr_control driver handle to the lock-free IQ ring:
 *   - start(): request stream at LO + rate, install an event handler that
 *     copies each borrowed IQ block into the ring (task 1.9).
 *   - The DSP task (1.10) later pops from the ring.
 *
 * The driver is configured in CALLBACK delivery mode, so every
 * EVT_IQ_BLOCK is a *borrowed* block: the pointer is valid only until the
 * handler returns. The copy into the PSRAM ring is the only work done in the
 * handler; it must be fast (< 1 ms).
 */

/**
 * Start streaming at the given LO and sample rate.
 *
 * Registers the IQ→ring event handler, then calls sdr_control_start().
 * Streaming state is reported asynchronously via driver events
 * (EVT_READY / EVT_STREAM_STARTED); poll sdr_stream_is_streaming() or watch
 * the serial log.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE if the ring or driver is not ready,
 *         or a driver error.
 */
esp_err_t sdr_stream_start(uint32_t frequency_hz, uint32_t sample_rate_sps);

/**
 * Stop streaming and clear the ring. Idempotent when already idle.
 */
esp_err_t sdr_stream_stop(void);

/**
 * True once a stream has started (last EVT_STREAM_STARTED observed and not yet
 * stopped). Set by the internal event handler.
 */
bool sdr_stream_is_streaming(void);

/**
 * Count of IQ blocks pushed into the ring since start (producer side).
 */
uint64_t sdr_stream_blocks_pushed(void);

/**
 * Last IQ block sequence number observed (driver-provided, monotonic).
 */
uint32_t sdr_stream_last_seq(void);

#ifdef __cplusplus
}
#endif
