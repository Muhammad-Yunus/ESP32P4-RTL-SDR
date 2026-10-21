/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_smoke.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "iq_ring.h"

static const char *TAG = "smoke";

/* M1 exit criteria (ARCHITECTURE / AGENT.md task 1.10). */
#define SDR_SMOKE_DURATION_MS  30000u
#define SDR_SMOKE_MIN_EFF_SPS  900000.0f

/* Single-shot smoke runner (V1). */
static sdr_smoke_result_t   s_res;
static SemaphoreHandle_t    s_done;
static volatile uint64_t    s_elapsed_us;

static void sdr_smoke_task(void *arg)
{
    (void)arg;
    int64_t t0 = esp_timer_get_time();   /* microseconds */
    int64_t t1;

    uint32_t blocks = 0;
    uint64_t bytes = 0;

    do {
        iq_ring_view_t view;
        esp_err_t ret = iq_ring_pop(&view);
        if (ret == ESP_OK && view.bytes > 0) {
            blocks++;
            bytes += view.bytes;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        t1 = esp_timer_get_time();
    } while ((t1 - t0) < (int64_t)SDR_SMOKE_DURATION_MS * 1000);

    s_elapsed_us = (uint64_t)(t1 - t0);
    float elapsed_s = (float)s_elapsed_us / 1e6f;

    s_res.blocks = blocks;
    s_res.bytes  = bytes;
    s_res.effective_sps  = (elapsed_s > 0.0f) ? ((float)bytes / 2.0f) / elapsed_s : 0.0f;
    s_res.overrun_bytes  = iq_ring_overrun_bytes();
    s_res.overrun_events = iq_ring_overrun_events();
    s_res.pass = (s_res.effective_sps > SDR_SMOKE_MIN_EFF_SPS)
                 && (s_res.overrun_events == 0);

    ESP_LOGI(TAG, "smoke: %u blocks, %llu bytes in %.1f s → effective_sps=%.0f "
             "overrun_bytes=%llu overrun_events=%u",
             s_res.blocks,
             (unsigned long long)s_res.bytes,
             elapsed_s,
             (double)s_res.effective_sps,
             (unsigned long long)s_res.overrun_bytes,
             s_res.overrun_events);

    if (s_res.pass) {
        ESP_LOGI(TAG, "SMOKE PASS — M1 exit criteria met "
             "(effective_sps > 900000, overruns == 0)");
    } else {
        ESP_LOGE(TAG, "SMOKE FAIL — criteria not met "
             "(effective_sps=%.0f, overrun_events=%u)",
             (double)s_res.effective_sps, s_res.overrun_events);
    }

    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t sdr_smoke_run(void)
{
    if (!iq_ring_is_ready()) {
        ESP_LOGE(TAG, "IQ ring not ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_done != NULL) {
        /* Already running or previous instance not cleaned up. */
        ESP_LOGW(TAG, "smoke already in flight");
        return ESP_ERR_INVALID_STATE;
    }

    /* Clean start. */
    memset(&s_res, 0, sizeof(s_res));
    s_elapsed_us = 0;
    iq_ring_reset();

    s_done = xSemaphoreCreateBinary();
    if (s_done == NULL) {
        ESP_LOGE(TAG, "semaphore create failed");
        return ESP_ERR_NO_MEM;
    }

    TaskHandle_t handle;
    BaseType_t ok = xTaskCreatePinnedToCore(
        sdr_smoke_task,
        "sdr_smoke",
        4096,
        NULL,
        12,
        &handle,
        0);   /* Core 0 per ARCHITECTURE.md Section 2 */
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "smoke task create failed");
        vSemaphoreDelete(s_done);
        s_done = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Block until the smoke task reports completion (30 s + margin). */
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(SDR_SMOKE_DURATION_MS + 5000)) != pdPASS) {
        ESP_LOGW(TAG, "smoke task did not complete in time");
    }
    vSemaphoreDelete(s_done);
    s_done = NULL;

    return ESP_OK;
}
