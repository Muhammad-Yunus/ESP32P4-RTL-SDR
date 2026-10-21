/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_stream.h"

#include "esp_log.h"
#include "esp_rtl_sdr.h"

#include <string.h>

static const char *TAG = "sdr_stream";

static volatile bool     s_streaming = false;
static volatile uint64_t s_blocks_pushed = 0;
static volatile uint32_t s_last_seq = 0;

/*
 * Event handler — runs on the driver's USB owner task. For EVT_IQ_BLOCK the
 * payload is a borrowed const esp_rtl_sdr_iq_block_t*; we copy it into the
 * lock-free ring and must return quickly. No lifecycle API calls here.
 */
static void sdr_stream_on_event(sdr_control_event_t event,
                                const void *payload,
                                void *ctx)
{
    (void)ctx;

    switch (event) {
    case SDR_CONTROL_EVT_ENUMERATED:
        ESP_LOGI(TAG, "enumerated");
        break;

    case SDR_CONTROL_EVT_READY:
        ESP_LOGI(TAG, "ready");
        break;

    case SDR_CONTROL_EVT_STREAM_STARTED:
        s_streaming = true;
        ESP_LOGI(TAG, "stream started");
        break;

    case SDR_CONTROL_EVT_IQ_BLOCK: {
        if (payload == NULL || !iq_ring_is_ready()) {
            break;
        }
        const esp_rtl_sdr_iq_block_t *blk =
            (const esp_rtl_sdr_iq_block_t *)payload;
        esp_err_t pr = iq_ring_push(blk->data, (uint32_t)blk->bytes);
        if (pr == ESP_OK) {
            s_last_seq = blk->sequence;
            s_blocks_pushed++;
        }
        /* Borrowed block: valid until we return. No explicit release needed
         * in CALLBACK delivery mode (acquire mode is off). */
        break;
    }

    case SDR_CONTROL_EVT_STOPPED:
        s_streaming = false;
        ESP_LOGI(TAG, "stream stopped");
        break;

    case SDR_CONTROL_EVT_DISCONNECTED:
        s_streaming = false;
        ESP_LOGW(TAG, "device disconnected");
        break;

    case SDR_CONTROL_EVT_RETUNED:
        ESP_LOGI(TAG, "retuned");
        break;

    case SDR_CONTROL_EVT_HEALTH:
        break;

    case SDR_CONTROL_EVT_ERROR:
        break;

    default:
        break;
    }
}

esp_err_t sdr_stream_start(uint32_t frequency_hz, uint32_t sample_rate_sps)
{
    if (!sdr_control_is_installed()) {
        ESP_LOGE(TAG, "start before driver install");
        return ESP_ERR_INVALID_STATE;
    }
    if (!iq_ring_is_ready()) {
        ESP_LOGE(TAG, "start before ring alloc");
        return ESP_ERR_INVALID_STATE;
    }

    /* Register the IQ→ring handler before starting the stream. */
    esp_err_t hr = sdr_control_set_event_handler(sdr_stream_on_event, NULL);
    if (hr != ESP_OK) {
        ESP_LOGE(TAG, "set_event_handler failed: %s", esp_err_to_name(hr));
        return hr;
    }

    s_streaming = false;
    s_blocks_pushed = 0;
    s_last_seq = 0;
    iq_ring_reset();

    esp_err_t sr = sdr_control_start(frequency_hz, sample_rate_sps);
    if (sr != ESP_OK) {
        ESP_LOGE(TAG, "sdr_control_start failed: %s",
                 esp_rtl_sdr_err_to_name(sr));
        return sr;
    }

    ESP_LOGI(TAG, "start requested: LO %u Hz @ %u SPS (awaiting EVT_STREAM_STARTED)",
             (unsigned)frequency_hz, (unsigned)sample_rate_sps);
    return ESP_OK;
}

esp_err_t sdr_stream_stop(void)
{
    if (!sdr_control_is_installed()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = sdr_control_stop();
    s_streaming = false;
    return ret;
}

bool sdr_stream_is_streaming(void)
{
    return s_streaming;
}

uint64_t sdr_stream_blocks_pushed(void)
{
    return s_blocks_pushed;
}

uint32_t sdr_stream_last_seq(void)
{
    return s_last_seq;
}
