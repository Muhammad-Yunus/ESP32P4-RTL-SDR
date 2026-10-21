/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * bsp_bringup — Phase 1 bring-up of the Waveshare Smart 86 Box BSP.
 *
 * Wires the display, audio, and USB host together in the correct order for
 * the first hardware milestone (M1). Higher layers (ui/, audio/, dsp/)
 * build on top of the handles stored here.
 *
 * Call bsp_bringup_init() once from app_main after sdr_control_init().
 */

/**
 * Initialize display (LVGL + MIPI-DSI panel), audio (I2S + ES8311 speaker),
 * and USB host. Logs status at each step.
 *
 * @return ESP_OK if all subsystems initialized, or the first ESP error code
 *         encountered (logging the failure). Display LVGL handle is set as a
 *         side effect and available via bsp_bringup_lv_display().
 */
esp_err_t bsp_bringup_init(void);

/**
 * Deinitialize display, audio, and USB host. Safe to call with NULL handles.
 */
void bsp_bringup_deinit(void);

/**
 * True after bsp_bringup_init() succeeded.
 */
bool bsp_bringup_is_ready(void);

/**
 * LVGL display handle (NULL before init / after deinit). Used by the UI
 * layer (ui/) to obtain the screen context.
 */
lv_display_t *bsp_bringup_lv_display(void);

/**
 * LVGL input device handle (touch). NULL before init.
 */
lv_indev_t *bsp_bringup_lv_indev(void);

#ifdef __cplusplus
}
#endif
