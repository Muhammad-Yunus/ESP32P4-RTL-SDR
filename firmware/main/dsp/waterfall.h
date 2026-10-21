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
 * waterfall — spectrum column → inferno color-mapped column in a
 * scrolling PSRAM buffer.
 *
 * The buffer is 720 px wide x 240 px tall, 1 byte/pixel (palette index),
 * column-major (ARCHITECTURE Section 4.2, memory layout 169 KiB PSRAM).
 * New columns enter at the left (col 0) and push old columns right.
 *
 * A UI image widget (Core 1) can call waterfall_read() to grab the full
 * buffer or the newest column.
 */

#define WATERFALL_WIDTH  720u  /* matches display width */
#define WATERFALL_HEIGHT 240u  /* 240 rows deep */

/**
 * Initialise the waterfall: allocates the PSRAM column buffer and builds the
 * 256-entry inferno colormap LUT. Must be called once before waterfall_push().
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t waterfall_init(void);

/** Free all waterfall resources. Idempotent. */
void waterfall_deinit(void);

/**
 * Push a new spectrum column.
 *
 * @param bins  int16 spectrum, WATERFALL_WIDTH bins (dB-scale 0..32767).
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t waterfall_push(const int16_t *bins);

/**
 * Copy the full 720x240 palette-index buffer (column-major) into `out`
 * (WATERFALL_WIDTH * WATERFALL_HEIGHT bytes). Safe to call from Core 1.
 */
esp_err_t waterfall_read(uint8_t *out);

/**
 * The 256-entry inferno palette as RGB565. index 0 = lowest, 255 = highest.
 * Allocated in waterfall_init(); valid until deinit.
 */
const uint16_t *waterfall_lut_rgb565(void);

/**
 * Number of columns pushed so far.
 */
uint32_t waterfall_column_count(void);

/**
 * True once waterfall_init() has succeeded.
 */
bool waterfall_is_ready(void);

#ifdef __cplusplus
}
#endif
