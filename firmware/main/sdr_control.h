/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_rtl_sdr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * sdr_control — thin lifecycle wrapper around esp_rtl_sdr v0.9.3.
 *
 * Owns the single RTL-SDR handle, the install configuration, and the event
 * callback plumbing. Higher layers (DSP, UI) interact with the SDR only
 * through this module so that driver quirks stay in one place.
 *
 * Threading: install/start/retune/stop are safe from any app task (the driver
 * serializes internally). They must NOT be called from the driver's event
 * callback. Use sdr_control_set_event_handler() to observe driver events on
 * the driver's USB owner task — that handler must return quickly (< 1 ms).
 */

/* Default tuning used by the boot sequence (KZEL 96.1 MHz reference). */
#define SDR_CONTROL_DEFAULT_FREQ_HZ  96100000u
#define SDR_CONTROL_DEFAULT_RATE_SP  ESP_RTL_SDR_RATE_960K

/*
 * Forwarded driver events. Kept as a small app-facing enum so the UI/DSP does
 * not depend on esp_rtl_sdr internals. The raw payload (if any) is passed
 * through untouched.
 */
typedef enum {
    SDR_CONTROL_EVT_ENUMERATED = 0,
    SDR_CONTROL_EVT_READY,
    SDR_CONTROL_EVT_STREAM_STARTED,
    SDR_CONTROL_EVT_IQ_BLOCK,
    SDR_CONTROL_EVT_STOPPED,
    SDR_CONTROL_EVT_ERROR,
    SDR_CONTROL_EVT_DISCONNECTED,
    SDR_CONTROL_EVT_RETUNED,
    SDR_CONTROL_EVT_HEALTH,
} sdr_control_event_t;

/*
 * Event handler signature. Runs on the driver's USB owner task; must not call
 * sdr_control_* lifecycle APIs and must not block.
 *
 * @param event   Forwarded event kind.
 * @param payload Event-specific payload (may be NULL). For IQ_BLOCK this is a
 *                const esp_rtl_sdr_iq_block_t* valid only until return.
 * @param ctx     User context supplied to sdr_control_set_event_handler().
 */
typedef void (*sdr_control_event_cb_t)(sdr_control_event_t event,
                                       const void *payload,
                                       void *ctx);

/**
 * Install the RTL-SDR driver and create the handle.
 *
 * Must be called once from the boot path before any other sdr_control_* call.
 * Idempotent: a second call while already installed returns ESP_ERR_INVALID_STATE.
 *
 * @return ESP_OK, or an esp_err_t / ESP_RTL_SDR_ERR_* value from the driver.
 */
esp_err_t sdr_control_init(void);

/**
 * Register (or clear, with handler == NULL) the app event handler.
 * Safe to call before or after sdr_control_init().
 */
esp_err_t sdr_control_set_event_handler(sdr_control_event_cb_t handler, void *ctx);

/**
 * Start streaming at the given LO and sample rate.
 * If frequency_hz is 0 the default LO is used; if sample_rate_sps is 0 the
 * default rate is used. The rate is validated against the driver window.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE if not installed / already streaming,
 *         or a driver error.
 */
esp_err_t sdr_control_start(uint32_t frequency_hz, uint32_t sample_rate_sps);

/**
 * Stop streaming. Idempotent when already idle.
 */
esp_err_t sdr_control_stop(void);

/**
 * Retune the LO while streaming (synchronous from an app task).
 */
esp_err_t sdr_control_retune(uint32_t frequency_hz);

/**
 * Manual tuner gain in tenths of dB (e.g. 496 == 49.6 dB).
 * Requires a claimed interface (i.e. after start). Forces manual gain mode.
 */
esp_err_t sdr_control_set_gain(int gain_tenth_db);

/**
 * Crystal / LO ppm correction, range [-200, +200].
 */
esp_err_t sdr_control_set_ppm(int ppm);

/**
 * Log a one-line summary of driver capabilities and device info.
 * Uses the sdr_ctrl TAG. Safe while idle or streaming.
 */
esp_err_t sdr_control_log_info(void);

/**
 * Log raw USB-host bus state, independent of the accepted-profile list.
 *
 * This distinguishes three cases that the candidate list cannot:
 *   - bus truly empty (device_count == 0, new_dev_events == 0)
 *   - device present but not accepted (device_count > 0, profile UNKNOWN)
 *   - device enumerating / stale after warm reset (new_dev > 0, gone == 0)
 *
 * Uses esp_rtl_sdr_usb_device_count() and esp_rtl_sdr_get_hub_stats(); no
 * handle is required for the count. Safe to call before a device attaches.
 */
esp_err_t sdr_control_log_usb_bus(void);

/**
 * Power-cycle the downstream ports of any external hub on the bus.
 *
 * Recovery for the "VBUS hard-tied to VCC_5V" case: after a warm MCU reset a
 * still-powered dongle can be left in a stale configured state, so the IDF
 * host stack goes silent and never enumerates it. Toggling the P4 root port
 * cannot remove power, so this asks the hub itself (hub-class requests) to
 * drop and restore downstream port power.
 *
 * @param off_ms  how long to hold port power off (100-1000 ms is sensible).
 * @return ESP_OK, or a driver error (ESP_ERR_NOT_FOUND when no hub exists).
 */
esp_err_t sdr_control_hub_power_cycle(uint32_t off_ms);

/**
 * The live handle (NULL before init / after deinit). For read-only queries.
 */
esp_rtl_sdr_handle_t sdr_control_get_handle(void);

/**
 * True once sdr_control_init() has succeeded.
 */
bool sdr_control_is_installed(void);

/**
 * Stop (if needed) and destroy the handle. Called on shutdown only.
 */
esp_err_t sdr_control_deinit(void);

#ifdef __cplusplus
}
#endif
