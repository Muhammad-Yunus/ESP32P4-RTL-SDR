/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_control.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sdr_ctrl";

/*
 * Single owned handle + install config. Access to the handle itself is
 * serialized by the driver; the fields below are only touched from app tasks
 * (boot path), never from the event callback.
 */
static esp_rtl_sdr_handle_t s_handle = NULL;
static bool s_installed = false;

/* App event handler + context, set before or after init. */
static sdr_control_event_cb_t s_event_handler = NULL;
static void *s_event_ctx = NULL;

/*
 * Driver event callback — runs on the USB owner task. Translate the driver
 * event enum to the app-facing enum and forward. Must not block.
 */
static void sdr_control_on_driver_event(esp_rtl_sdr_event_t event,
                                        const void *payload,
                                        void *user_ctx)
{
    (void)user_ctx;

    sdr_control_event_t out;
    switch (event) {
    case ESP_RTL_SDR_EVT_ENUMERATED:       out = SDR_CONTROL_EVT_ENUMERATED;     break;
    case ESP_RTL_SDR_EVT_READY:            out = SDR_CONTROL_EVT_READY;          break;
    case ESP_RTL_SDR_EVT_STREAM_STARTED:   out = SDR_CONTROL_EVT_STREAM_STARTED; break;
    case ESP_RTL_SDR_EVT_IQ_BLOCK:         out = SDR_CONTROL_EVT_IQ_BLOCK;       break;
    case ESP_RTL_SDR_EVT_STOPPED:          out = SDR_CONTROL_EVT_STOPPED;        break;
    case ESP_RTL_SDR_EVT_ERROR:            out = SDR_CONTROL_EVT_ERROR;          break;
    case ESP_RTL_SDR_EVT_DISCONNECTED:     out = SDR_CONTROL_EVT_DISCONNECTED;   break;
    case ESP_RTL_SDR_EVT_RETUNED:          out = SDR_CONTROL_EVT_RETUNED;        break;
    case ESP_RTL_SDR_EVT_HEALTH:           out = SDR_CONTROL_EVT_HEALTH;         break;
    default:
        /* Passport events are not surfaced to the app in V1. */
        return;
    }

    if (event == ESP_RTL_SDR_EVT_ERROR && payload != NULL) {
        const esp_rtl_sdr_error_info_t *err = payload;
        ESP_LOGE(TAG, "driver error: %s (%s)",
                 esp_rtl_sdr_err_to_name(err->code), err->message);
    }

    if (s_event_handler != NULL) {
        s_event_handler(out, payload, s_event_ctx);
    }
}

esp_err_t sdr_control_init(void)
{
    if (s_installed) {
        ESP_LOGE(TAG, "already installed");
        return ESP_ERR_INVALID_STATE;
    }

    esp_rtl_sdr_config_t cfg;
    esp_rtl_sdr_config_default(&cfg);
    cfg.delivery_mode = ESP_RTL_SDR_DELIVERY_CALLBACK;
    cfg.event_cb = sdr_control_on_driver_event;
    cfg.event_ctx = NULL;
    cfg.usb_task_core_id = 0;

    esp_err_t ret = esp_rtl_sdr_install(&cfg, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "install failed: %s", esp_rtl_sdr_err_to_name(ret));
        s_handle = NULL;
        return ret;
    }

    s_installed = true;
    ESP_LOGI(TAG, "installed (driver v%s)", esp_rtl_sdr_get_version_string());
    return ESP_OK;
}

esp_err_t sdr_control_set_event_handler(sdr_control_event_cb_t handler, void *ctx)
{
    s_event_handler = handler;
    s_event_ctx = ctx;
    return ESP_OK;
}

esp_err_t sdr_control_start(uint32_t frequency_hz, uint32_t sample_rate_sps)
{
    if (!s_installed) {
        ESP_LOGE(TAG, "start before init");
        return ESP_ERR_INVALID_STATE;
    }

    if (frequency_hz == 0) {
        frequency_hz = SDR_CONTROL_DEFAULT_FREQ_HZ;
    }
    if (sample_rate_sps == 0) {
        sample_rate_sps = SDR_CONTROL_DEFAULT_RATE_SP;
    }

    if (!esp_rtl_sdr_is_rate_supported(sample_rate_sps)) {
        ESP_LOGE(TAG, "unsupported sample rate: %u Hz", (unsigned)sample_rate_sps);
        return ESP_RTL_SDR_ERR_BAD_RATE;
    }

    esp_err_t ret = esp_rtl_sdr_start_hz(s_handle, frequency_hz, sample_rate_sps);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start_hz(%u, %u) failed: %s",
                 (unsigned)frequency_hz, (unsigned)sample_rate_sps,
                 esp_rtl_sdr_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "start requested: LO %u Hz @ %u SPS",
             (unsigned)frequency_hz, (unsigned)sample_rate_sps);
    return ESP_OK;
}

esp_err_t sdr_control_stop(void)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_rtl_sdr_stop(s_handle, 0);
}

esp_err_t sdr_control_retune(uint32_t frequency_hz)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_rtl_sdr_retune_hz(s_handle, frequency_hz);
}

esp_err_t sdr_control_set_gain(int gain_tenth_db)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_rtl_sdr_set_tuner_gain(s_handle, gain_tenth_db);
}

esp_err_t sdr_control_set_ppm(int ppm)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_rtl_sdr_set_freq_correction(s_handle, ppm);
}

esp_err_t sdr_control_log_info(void)
{
    if (!s_installed) {
        ESP_LOGW(TAG, "not installed");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t caps = esp_rtl_sdr_get_device_capabilities(s_handle);
    ESP_LOGI(TAG, "profile=%s caps=0x%08x%s%s%s%s",
             esp_rtl_sdr_profile_to_name(esp_rtl_sdr_get_profile(s_handle)),
             (unsigned)caps,
             (caps & ESP_RTL_SDR_CAP_STREAM) ? " STREAM" : "",
             (caps & ESP_RTL_SDR_CAP_RETUNE) ? " RETUNE" : "",
             (caps & ESP_RTL_SDR_CAP_GAIN) ? " GAIN" : "",
             (caps & ESP_RTL_SDR_CAP_DIRECT_SAMPLING) ? " DIRECT_SAMPLING" : "");

    esp_rtl_sdr_device_info_t info;
    if (esp_rtl_sdr_get_device_info(s_handle, &info) == ESP_OK) {
        if (info.present) {
            ESP_LOGI(TAG, "device %04x:%04x present=%d hs=%d mfr='%s' prod='%s'",
                     info.vid, info.pid, (int)info.present, (int)info.high_speed,
                     info.manufacturer, info.product);
        } else {
            ESP_LOGW(TAG, "no accepted device attached yet");
        }
    }

    return ESP_OK;
}

esp_err_t sdr_control_log_usb_bus(void)
{
    size_t raw_count = 0;
    esp_err_t ret = esp_rtl_sdr_usb_device_count(&raw_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "usb_device_count failed: %s", esp_rtl_sdr_err_to_name(ret));
    }

    esp_rtl_sdr_hub_stats_t hub;
    esp_rtl_sdr_hub_stats_default(&hub);
    esp_err_t hret = esp_rtl_sdr_get_hub_stats(s_handle, &hub);
    if (hret != ESP_OK) {
        ESP_LOGW(TAG, "hub_stats failed: %s", esp_rtl_sdr_err_to_name(hret));
    }

    ESP_LOGI(TAG, "usb bus: raw_devices=%u new_dev=%u gone=%u claims=%zu claimed=%u host=%d hubs=%d",
             (unsigned)raw_count,
             (unsigned)hub.new_dev_events,
             (unsigned)hub.gone_events,
             (size_t)hub.session_refcount,
             (unsigned)hub.claimed_rtl_count,
             (int)hub.host_installed,
             (int)hub.hubs_compiled_in);

    if (s_installed) {
        size_t cand = 0;
        if (esp_rtl_sdr_get_device_count(s_handle, &cand) == ESP_OK) {
            ESP_LOGI(TAG, "usb bus: accepted_candidates=%u profile=%s",
                     (unsigned)cand,
                     esp_rtl_sdr_profile_to_name(esp_rtl_sdr_get_profile(s_handle)));
        }
    }

    return ESP_OK;
}

esp_err_t sdr_control_hub_power_cycle(uint32_t off_ms)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (off_ms == 0) {
        off_ms = 300;
    }
    esp_err_t ret = esp_rtl_sdr_hub_port_power_cycle(s_handle, off_ms);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "hub port power cycled (off %u ms)", (unsigned)off_ms);
    } else {
        ESP_LOGW(TAG, "hub power cycle: %s", esp_rtl_sdr_err_to_name(ret));
    }
    return ret;
}

esp_rtl_sdr_handle_t sdr_control_get_handle(void)
{
    return s_handle;
}

bool sdr_control_is_installed(void)
{
    return s_installed;
}

esp_err_t sdr_control_deinit(void)
{
    if (!s_installed) {
        return ESP_OK;
    }

    esp_rtl_sdr_state_t state = esp_rtl_sdr_get_state(s_handle);
    if (state == ESP_RTL_SDR_STATE_STREAMING ||
        state == ESP_RTL_SDR_STATE_STOPPING ||
        state == ESP_RTL_SDR_STATE_STARTING) {
        (void)esp_rtl_sdr_stop(s_handle, 0);
    }

    esp_err_t ret = esp_rtl_sdr_uninstall(s_handle);
    s_handle = NULL;
    s_installed = false;
    s_event_handler = NULL;
    s_event_ctx = NULL;
    return ret;
}
