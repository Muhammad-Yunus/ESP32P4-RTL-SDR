/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "fft.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "dsps_fft4r.h"

#include <string.h>

static const char *TAG = "dsp_fft";

/* Twiddle table must be resident for the radix-4 FFT; the table for a
 * 1024-point transform is 1024 complex re/im float32 = 8 KiB, kept in
 * PSRAM (large allocation) — the arch memory rules allow FFT scratch in
 * PSRAM because the P4-optimized kernel is latency-tolerant here. */
static float  *s_twiddle = NULL;
static float  *s_work    = NULL;  /* reusable complex work buffer, 2*FFT_SIZE */
static bool     s_ready   = false;

esp_err_t fft_init(void)
{
    if (s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Init the twiddle table into our own PSRAM buffer (not the global). */
    esp_err_t ret = dsps_fft4r_init_fc32(s_twiddle, (int)FFT_SIZE);
    if (ret != ESP_OK) {
        /* Allocate PSRAM table if caller left s_twiddle NULL. */
        s_twiddle = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float),
                                     MALLOC_CAP_SPIRAM);
        if (!s_twiddle) {
            ESP_LOGE(TAG, "twiddle table PSRAM alloc failed");
            return ESP_ERR_NO_MEM;
        }
        ret = dsps_fft4r_init_fc32(s_twiddle, (int)FFT_SIZE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "fft4r init failed: %d", ret);
            heap_caps_free(s_twiddle);
            s_twiddle = NULL;
            return ret;
        }
    }

    s_work = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!s_work) {
        ESP_LOGE(TAG, "work buffer PSRAM alloc failed");
        dsps_fft4r_deinit_fc32();
        heap_caps_free(s_twiddle);
        s_twiddle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "fft init ok: N=%u twiddle+work in PSRAM", (unsigned)FFT_SIZE);
    return ESP_OK;
}

void fft_deinit(void)
{
    if (!s_ready) {
        return;
    }
    if (s_work)    { heap_caps_free(s_work);    s_work = NULL; }
    if (s_twiddle) { heap_caps_free(s_twiddle); s_twiddle = NULL; }
    s_ready = false;
    ESP_LOGI(TAG, "fft deinit");
}

esp_err_t fft_run(float *data)
{
    if (!s_ready) {
        ESP_LOGE(TAG, "run before init");
        return ESP_ERR_INVALID_STATE;
    }
    /* dsps_fft4r_fc32 auto-selects the P4-optimized kernel via #define. */
    esp_err_t ret = dsps_fft4r_fc32(data, (int)FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "fft4r run failed: %d", ret);
    }
    return ret;
}

size_t fft_work_elems(void)
{
    return FFT_SIZE * 2u;
}

bool fft_is_ready(void)
{
    return s_ready;
}
