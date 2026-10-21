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
 * spectrum — DC offset removal + Hann + FFT + 512-bin power + peak-hold.
 *
 * Consumes CU8 interleaved IQ bytes from the IQ ring and produces a 512-bin
 * power spectrum (dBm relative, int16) in a double-buffer. UI (Core 1) reads
 * the newest buffer via spectrum_read().
 *
 * Processing model (ARCHITECTURE Section 4.1): per 1024-sample frame,
 *   CU8 -> int16 -> DC-remove -> Hann -> FFT 1024 -> |X|^2 -> dBm ->
 *   pair-average to 512 bins -> peak-hold -> double-buffer publish.
 */

#define SPECTRUM_BINS       512u
#define SPECTRUM_FRAME_LEN  1024u  /* complex samples per FFT */

/** dBm reference floor / scale for the int16 mapping (see ARCHITECTURE 4.1). */
#define SPECTRUM_DB_FULL    0.0f    /* 0 dBm maps to +32767 */
#define SPECTRUM_DB_FLOOR  (-120.0f) /* -120 dBm maps to 0 */

/**
 * Initialise the spectrum processor: allocates the Hann window, FFT scratch
 * (via fft_init), peak-hold and double-buffer. Must be called once.
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t spectrum_init(void);

/** Free all spectrum resources. Idempotent. */
void spectrum_deinit(void);

/**
 * Process one CU8 IQ buffer of `bytes` bytes into the peak-hold spectrum.
 * Called from the sdr_dsp task (Core 0). May be called with any length
 * (frames are consumed incrementally; partial frames accumulate).
 *
 * @param data  CU8 interleaved IQ (I0 Q0 I1 Q1 ...), 2 bytes/sample.
 * @param bytes number of bytes (even).
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t spectrum_process(const uint8_t *data, uint32_t bytes);

/**
 * Read the newest published 512-bin spectrum (int16 dBm values).
 * Safe to call from Core 1 (UI). Copies into `out`.
 * @param out  Array of SPECTRUM_BINS int16.
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t spectrum_read(int16_t *out);

/**
 * The current center frequency (Hz) the spectrum corresponds to.
 * Updated by the caller before process(); default 0 until set.
 */
void spectrum_set_center_hz(uint32_t hz);
uint32_t spectrum_center_hz(void);

/**
 * The sample rate (Hz) in use (for Hz-per-bin scaling). Default 960000.
 */
void spectrum_set_rate_hz(uint32_t hz);
uint32_t spectrum_rate_hz(void);

/** Peak power bin index (0..SPECTRUM_BINS-1) of the newest frame. */
uint32_t spectrum_peak_bin(void);

#ifdef __cplusplus
}
#endif
