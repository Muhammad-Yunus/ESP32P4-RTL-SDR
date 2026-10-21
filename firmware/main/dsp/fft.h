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
 * fft — thin wrapper over esp-dsp's radix-4 complex FFT.
 *
 * Allocates the FFT twiddle table (SRAM, hot scratch) and a reusable
 * complex float32 work buffer. A call to fft_run() transforms in place.
 *
 * N must be a power of four (1024 = 4^5 is valid for esp-dsp radix-4).
 */

/** Fixed transform size (CONVENTIONS / ARCHITECTURE Section 4.1). */
#define FFT_SIZE 1024u

/**
 * Initialise the FFT: allocates the twiddle table and work buffer.
 * Must be called once before fft_run(). Idempotent-safe: returns
 * ESP_ERR_INVALID_STATE if called twice.
 * @return ESP_OK on success, ESP_ERR_NO_MEM on allocation failure.
 */
esp_err_t fft_init(void);

/**
 * Free FFT resources. Safe to call when not initialised.
 */
void fft_deinit(void);

/**
 * Compute a forward complex FFT in place.
 *
 * @param data  Complex float32 array of FFT_SIZE elements, layout
 *              Re[0],Im[0],Re[1],Im[1],... (i.e. 2*FFT_SIZE floats).
 *              On return it holds the FFT result (unbit-reversed layout as
 *              produced by dsps_fft4r_fc32).
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t fft_run(float *data);

/**
 * Number of real float32 elements (2*FFT_SIZE) the work buffer holds.
 */
size_t fft_work_elems(void);

/**
 * True once fft_init() has succeeded.
 */
bool fft_is_ready(void);

#ifdef __cplusplus
}
#endif
