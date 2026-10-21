/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_dsp.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "iq_ring.h"
#include "spectrum.h"
#include "sdr_control.h"

static const char *TAG = "sdr_dsp";

static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;
static volatile uint64_t s_blocks = 0;
static SemaphoreHandle_t s_stop_sem = NULL;

static void sdr_dsp_task(void *arg)
{
    (void)arg;

    /* Align the spectrum center/rate with the driver's last tune. */
    spectrum_set_center_hz(sdr_control_last_freq_hz());
    spectrum_set_rate_hz(sdr_control_last_rate_sps());

    int64_t last_metric = esp_timer_get_time();

    while (s_running) {
        iq_ring_view_t view;
        esp_err_t ret = iq_ring_pop(&view);
        if (ret == ESP_OK && view.bytes > 0) {
            esp_err_t sr = spectrum_process(view.data, view.bytes);
            if (sr == ESP_OK) {
                s_blocks++;
            }
        } else {
            /* Ring empty: yield briefly. */
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        /* 1 Hz metric line (CONVENTIONS 5.3). */
        int64_t now = esp_timer_get_time();
        if (now - last_metric >= 1000000) {
            int16_t spec[SPECTRUM_BINS];
            if (spectrum_read(spec) == ESP_OK) {
                int16_t pk = 0;
                for (uint32_t b = 0; b < SPECTRUM_BINS; b++) {
                    if (spec[b] > pk) pk = spec[b];
                }
                ESP_LOGI(TAG, "blocks=%llu ring_overrun=%u peak_bin=%u "
                         "spec_peak_i16=%d center=%u Hz rate=%u Hz",
                         (unsigned long long)s_blocks,
                         (unsigned)iq_ring_overrun_events(),
                         (unsigned)spectrum_peak_bin(),
                         (int)pk,
                         (unsigned)spectrum_center_hz(),
                         (unsigned)spectrum_rate_hz());
            }
            last_metric = now;
        }
    }

    xSemaphoreGive(s_stop_sem);
    vTaskDelete(NULL);
}

esp_err_t sdr_dsp_start(void)
{
    if (s_task != NULL) {
        ESP_LOGW(TAG, "already started");
        return ESP_ERR_INVALID_STATE;
    }
    if (!iq_ring_is_ready()) {
        ESP_LOGE(TAG, "IQ ring not ready");
        return ESP_ERR_INVALID_STATE;
    }

    s_stop_sem = xSemaphoreCreateBinary();
    if (s_stop_sem == NULL) {
        ESP_LOGE(TAG, "stop sem create failed");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    s_blocks = 0;

    TaskHandle_t handle;
    BaseType_t ok = xTaskCreatePinnedToCore(
        sdr_dsp_task,
        "sdr_dsp",
        16384,      /* 16 K stack (ARCHITECTURE Section 2) */
        NULL,
        12,         /* prio 12 */
        &handle,
        0);         /* Core 0 */
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        s_running = false;
        vSemaphoreDelete(s_stop_sem);
        s_stop_sem = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_task = handle;
    ESP_LOGI(TAG, "sdr_dsp task started (Core 0, prio 12)");
    return ESP_OK;
}

esp_err_t sdr_dsp_stop(void)
{
    if (s_task == NULL) {
        return ESP_OK;
    }
    s_running = false;
    if (s_stop_sem != NULL) {
        xSemaphoreTake(s_stop_sem, pdMS_TO_TICKS(2000));
        vSemaphoreDelete(s_stop_sem);
        s_stop_sem = NULL;
    }
    s_task = NULL;
    ESP_LOGI(TAG, "sdr_dsp task stopped (consumed %llu blocks)",
             (unsigned long long)s_blocks);
    return ESP_OK;
}

uint64_t sdr_dsp_blocks_consumed(void)
{
    return s_blocks;
}
