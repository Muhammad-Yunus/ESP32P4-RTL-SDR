/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "spectrum.h"
#include "fft.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include <math.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "dsp_spec";

/* ---- State ---- */
static float   *s_hann      = NULL;  /* FFT_SIZE Hann coeffs (PSRAM) */
static float   *s_frame     = NULL;  /* 2*FFT_SIZE complex float accumulator */
static int16_t *s_peak      = NULL;  /* peak-hold, SPECTRUM_BINS */
static int16_t *s_dbuf[2]   = { NULL, NULL }; /* double-buffer, SPECTRUM_BINS */
static volatile uint32_t s_write_idx = 0;
static volatile uint32_t s_read_idx  = 0;
static volatile uint32_t s_seq       = 0;

static uint32_t s_center_hz = 0;
static uint32_t s_rate_hz   = 960000u;

/* Frame accumulation across multiple process() calls. */
static uint16_t s_frame_samples_used = 0;  /* 0..SPECTRUM_FRAME_LEN */

/* 12-bit scaling factor to keep |X|^2 in float range, then dB. */
#define DBM_TO_I16(x) ((x) >= SPECTRUM_DB_FULL ? 32767 : \
                       (x) <= SPECTRUM_DB_FLOOR ? 0 : (int16_t)( \
                         ( (x) - SPECTRUM_DB_FLOOR ) / ( SPECTRUM_DB_FULL - SPECTRUM_DB_FLOOR ) * 32767.0f))

esp_err_t spectrum_init(void)
{
    if (fft_is_ready()) {
        /* FFT already initialised by an earlier call. */
    } else {
        esp_err_t fr = fft_init();
        if (fr != ESP_OK) return fr;
    }

    s_hann = (float *)heap_caps_malloc(FFT_SIZE * sizeof(float), MALLOC_CAP_SPIRAM);
    s_frame = (float *)heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    s_peak = (int16_t *)heap_caps_malloc(SPECTRUM_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_dbuf[0] = (int16_t *)heap_caps_malloc(SPECTRUM_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_dbuf[1] = (int16_t *)heap_caps_malloc(SPECTRUM_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_hann || !s_frame || !s_peak || !s_dbuf[0] || !s_dbuf[1]) {
        ESP_LOGE(TAG, "PSRAM alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* Build the Hann window: w[n] = 0.5(1 - cos(2*pi*n/N)). */
    for (uint32_t n = 0; n < FFT_SIZE; n++) {
        s_hann[n] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)n / (float)FFT_SIZE));
    }

    memset(s_peak, 0, SPECTRUM_BINS * sizeof(int16_t));
    memset(s_dbuf[0], 0, SPECTRUM_BINS * sizeof(int16_t));
    memset(s_dbuf[1], 0, SPECTRUM_BINS * sizeof(int16_t));
    s_frame_samples_used = 0;

    ESP_LOGI(TAG, "init ok: FFT=%u bins=%u frame=%u samples",
             (unsigned)FFT_SIZE, (unsigned)SPECTRUM_BINS, (unsigned)SPECTRUM_FRAME_LEN);
    return ESP_OK;
}

void spectrum_deinit(void)
{
    fft_deinit();
    if (s_hann)   { heap_caps_free(s_hann);   s_hann = NULL; }
    if (s_frame)  { heap_caps_free(s_frame);  s_frame = NULL; }
    if (s_peak)   { heap_caps_free(s_peak);   s_peak = NULL; }
    if (s_dbuf[0]){ heap_caps_free(s_dbuf[0]); s_dbuf[0] = NULL; }
    if (s_dbuf[1]){ heap_caps_free(s_dbuf[1]); s_dbuf[1] = NULL; }
    s_frame_samples_used = 0;
    ESP_LOGI(TAG, "deinit");
}

/*
 * DC offset removal: subtract the running frame mean of I and Q.
 * Operates on s_frame (interleaved float Re/Im of frame_samples_used samples).
 */
static void sdc_remove(uint32_t n)
{
    if (n == 0) return;
    float mean_i = 0.0f, mean_q = 0.0f;
    for (uint32_t k = 0; k < n; k++) {
        mean_i += s_frame[2*k];
        mean_q += s_frame[2*k + 1];
    }
    mean_i /= (float)n;
    mean_q /= (float)n;
    for (uint32_t k = 0; k < n; k++) {
        s_frame[2*k]     -= mean_i;
        s_frame[2*k + 1] -= mean_q;
    }
}

/* File-scope peak bin (exposed). */
static volatile uint32_t s_peak_bin = 0;

esp_err_t spectrum_process(const uint8_t *data, uint32_t bytes)
{
    if (!fft_is_ready() || s_frame == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t samples_this_call = bytes / 2u;  /* CU8: 2 bytes per sample */
    uint32_t remaining_capacity = SPECTRUM_FRAME_LEN - s_frame_samples_used;

    /* Fill the frame accumulator in chunks. */
    uint32_t take = (samples_this_call < remaining_capacity)
                        ? samples_this_call : remaining_capacity;
    for (uint32_t i = 0; i < take; i++) {
        /* CU8 to float, bias removed (-128 to make signed), scaled to [-1,1]. */
        int16_t iv = (int16_t)data[2*i]     - 128;
        int16_t qv = (int16_t)data[2*i + 1] - 128;
        s_frame[2*(s_frame_samples_used + i)]     = (float)iv / 128.0f;
        s_frame[2*(s_frame_samples_used + i) + 1] = (float)qv / 128.0f;
    }
    s_frame_samples_used += take;

    /* If more data than one frame, drop the overflow (frame already full). */
    if (samples_this_call > remaining_capacity) {
        /* We consumed `take == remaining_capacity`; the rest is discarded. */
        /* Nothing to do — just fall through to FFT on the full frame. */
    }

    if (s_frame_samples_used < SPECTRUM_FRAME_LEN) {
        return ESP_OK;  /* frame not yet complete */
    }

    /* ---- Frame complete: run FFT + power + peak-hold + publish ---- */

    /* DC offset removal. */
    sdc_remove(SPECTRUM_FRAME_LEN);

    /* Hann window (in place). */
    for (uint32_t n = 0; n < FFT_SIZE; n++) {
        s_frame[2*n]     *= s_hann[n];
        s_frame[2*n + 1] *= s_hann[n];
    }

    /* FFT (in place on s_frame). */
    esp_err_t fr = fft_run(s_frame);
    if (fr != ESP_OK) {
        s_frame_samples_used = 0;
        return fr;
    }

    /* Power spectrum: P[k] = (|X[k]|^2 / N^2) * scale → dBm.
     * N = FFT_SIZE. Scale so that full-scale input ≈ 0 dBm at bin. */
    const float norm = 1.0f / ((float)FFT_SIZE * (float)FFT_SIZE);

    int16_t new_spec[SPECTRUM_BINS];
    /* Only first 512 bins are meaningful (0..Fs/2); pair-average for 512. */
    uint32_t peak_bin = 0;
    for (uint32_t b = 0; b < SPECTRUM_BINS; b++) {
        /* Average bins b and b+512 (mirrored) to improve SNR. */
        float re = s_frame[2*b] + s_frame[2*(b + SPECTRUM_BINS)];
        float im = s_frame[2*b + 1] + s_frame[2*(b + SPECTRUM_BINS) + 1];
        float pwr = (re*re + im*im) * norm;
        /* Convert to dBm relative (0 dBm = full scale). */
        float db = (pwr > 1e-12f) ? 10.0f * log10f(pwr) : -120.0f;
        int16_t v = DBM_TO_I16(db);
        new_spec[b] = v;
        /* Peak-hold update. */
        if (v > s_peak[b]) {
            s_peak[b] = v;
        }
        if (v > new_spec[peak_bin]) {
            peak_bin = b;
        }
    }

    /* Peak-hold decay: not strictly per-frame here; UI can handle decay. */

    /* Publish to the next double-buffer slot, seq increment atomically last. */
    uint32_t wr = s_write_idx;
    memcpy(s_dbuf[wr], new_spec, SPECTRUM_BINS * sizeof(int16_t));
    atomic_thread_fence(memory_order_seq_cst);
    s_seq++;
    s_write_idx = wr ^ 1;

    s_frame_samples_used = 0;  /* reset accumulator for next frame */

    /* Store the peak bin for UI. */
    /* We expose via a small static (kept in a file-scope var for brevity). */
    s_peak_bin = peak_bin;

    return ESP_OK;
}

esp_err_t spectrum_read(int16_t *out)
{
    if (out == NULL || s_dbuf[0] == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Read the newest slot: the slot that was most recently written. */
    uint32_t rd = s_read_idx;  /* reader reads the slot that is NOT being written */
    /* The writer writes s_write_idx; the reader reads the other one. */
    rd = s_write_idx ^ 1;
    atomic_thread_fence(memory_order_seq_cst);
    memcpy(out, s_dbuf[rd], SPECTRUM_BINS * sizeof(int16_t));
    s_read_idx = rd;
    return ESP_OK;
}

void spectrum_set_center_hz(uint32_t hz) { s_center_hz = hz; }
uint32_t spectrum_center_hz(void)          { return s_center_hz; }

void spectrum_set_rate_hz(uint32_t hz)     { s_rate_hz = hz; }
uint32_t spectrum_rate_hz(void)            { return s_rate_hz; }

uint32_t spectrum_peak_bin(void) { return s_peak_bin; }
