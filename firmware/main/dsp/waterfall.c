/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "waterfall.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include <math.h>
#include <string.h>

static const char *TAG = "dsp_wf";

/* ---- State ---- */
static uint8_t   *s_buf   = NULL;   /* WATERFALL_WIDTH * WATERFALL_HEIGHT palette-idx */
static uint16_t  *s_lut   = NULL;   /* 256 inferno RGB565 (PSRAM, built at init) */
static uint32_t   s_cols  = 0;      /* rows pushed so far */
static bool        s_ready = false;

/*
 * Inferno colormap anchors (matplotlib 'inferno'). 17 control points at
 * t = 0, 1/16, ..., 1 (0..1). R,G,B in 0..1. Interpolated to 256 entries
 * at init — avoids hard-coding 256 RGB565 words and guarantees colors.
 */
typedef struct { float t, r, g, b; } inferno_anchor_t;
static const inferno_anchor_t s_inferno_anchors[] = {
    { 0.000f, 0.0004f, 0.0000f, 0.0134f },
    { 0.0625f, 0.0444f, 0.0042f, 0.1931f },
    { 0.1250f, 0.1512f, 0.0356f, 0.3111f },
    { 0.1875f, 0.3158f, 0.0888f, 0.3702f },
    { 0.2500f, 0.4878f, 0.1440f, 0.3884f },
    { 0.3125f, 0.6421f, 0.2198f, 0.3824f },
    { 0.3750f, 0.7812f, 0.3084f, 0.3543f },
    { 0.4375f, 0.8903f, 0.4104f, 0.3046f },
    { 0.5000f, 0.9746f, 0.5242f, 0.2387f },
    { 0.5625f, 0.9990f, 0.6494f, 0.1749f },
    { 0.6250f, 0.9995f, 0.7759f, 0.1258f },
    { 0.6875f, 0.9982f, 0.8885f, 0.0862f },
    { 0.7500f, 0.9966f, 0.9708f, 0.0504f },
    { 0.8125f, 0.9730f, 1.0000f, 0.0039f },
    { 0.8750f, 0.9364f, 0.9997f, 0.0970f },
    { 0.9375f, 0.8696f, 0.9800f, 0.2395f },
    { 1.000f, 1.0000f, 1.0000f, 0.5956f },
};
#define INFERNO_NANCHORS (sizeof(s_inferno_anchors) / sizeof(s_inferno_anchors[0]))

static void build_inferno_lut(uint16_t *lut)
{
    const size_t na = INFERNO_NANCHORS;
    for (int i = 0; i < 256; i++) {
        float t = (float)i / 255.0f;
        size_t lo = 0;
        while (lo < na - 1 && s_inferno_anchors[lo + 1].t <= t) lo++;
        if (lo >= na - 1) lo = na - 2;
        size_t hi = lo + 1;
        float span = s_inferno_anchors[hi].t - s_inferno_anchors[lo].t;
        float f = (span > 0.0f) ? (t - s_inferno_anchors[lo].t) / span : 0.0f;
        float r = s_inferno_anchors[lo].r + (s_inferno_anchors[hi].r - s_inferno_anchors[lo].r) * f;
        float g = s_inferno_anchors[lo].g + (s_inferno_anchors[hi].g - s_inferno_anchors[lo].g) * f;
        float b = s_inferno_anchors[lo].b + (s_inferno_anchors[hi].b - s_inferno_anchors[lo].b) * f;
        if (r < 0.0f) { r = 0.0f; }
        if (r > 1.0f) { r = 1.0f; }
        if (g < 0.0f) { g = 0.0f; }
        if (g > 1.0f) { g = 1.0f; }
        if (b < 0.0f) { b = 0.0f; }
        if (b > 1.0f) { b = 1.0f; }
        int ri = (int)(r * 255.0f + 0.5f);
        int gi = (int)(g * 255.0f + 0.5f);
        int bi = (int)(b * 255.0f + 0.5f);
        if (ri < 0)   { ri = 0; }
        if (ri > 255) { ri = 255; }
        if (gi < 0)   { gi = 0; }
        if (gi > 255) { gi = 255; }
        if (bi < 0)   { bi = 0; }
        if (bi > 255) { bi = 255; }
        lut[i] = (uint16_t)(((ri & 0xF8) << 8) | ((gi & 0xFC) << 3) | (bi >> 3));
    }
}

esp_err_t waterfall_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    size_t buf_sz = (size_t)WATERFALL_WIDTH * WATERFALL_HEIGHT;
    s_buf = (uint8_t *)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (!s_buf) {
        ESP_LOGE(TAG, "PSRAM buffer alloc failed (%u bytes)", (unsigned)buf_sz);
        return ESP_ERR_NO_MEM;
    }
    memset(s_buf, 0, buf_sz);

    s_lut = (uint16_t *)heap_caps_malloc(256 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_lut) {
        ESP_LOGE(TAG, "LUT alloc failed");
        heap_caps_free(s_buf);
        s_buf = NULL;
        return ESP_ERR_NO_MEM;
    }
    build_inferno_lut(s_lut);

    s_cols  = 0;
    s_ready = true;

    ESP_LOGI(TAG, "init ok: %ux%u column-major, %u KiB PSRAM, inferno LUT (runtime)",
             (unsigned)WATERFALL_WIDTH, (unsigned)WATERFALL_HEIGHT,
             (unsigned)(buf_sz / 1024u));
    return ESP_OK;
}

void waterfall_deinit(void)
{
    if (!s_ready) {
        return;
    }
    if (s_buf) { heap_caps_free(s_buf); s_buf = NULL; }
    if (s_lut) { heap_caps_free(s_lut); s_lut = NULL; }
    s_ready = false;
    ESP_LOGI(TAG, "deinit");
}

esp_err_t waterfall_push(const int16_t *bins)
{
    if (!s_ready || bins == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Row-scroll: move each row down by one pixel (top row is newest). */
    size_t row_bytes = (size_t)WATERFALL_WIDTH;
    memmove(s_buf + row_bytes, s_buf, row_bytes * (WATERFALL_HEIGHT - 1u));

    /* New top row: 512 bins -> 720 px via nearest-neighbor. */
    const uint32_t num_bins = 512u;
    for (uint32_t px = 0; px < WATERFALL_WIDTH; px++) {
        uint32_t bin = (px * num_bins) / WATERFALL_WIDTH;
        int16_t  v   = bins[bin];
        uint8_t norm = (v > 0) ? (uint8_t)(((uint32_t)v >> 7) & 0xFFu) : 0u;
        s_buf[px] = norm;   /* palette index into inferno LUT */
    }

    s_cols++;
    return ESP_OK;
}

esp_err_t waterfall_read(uint8_t *out)
{
    if (!s_ready || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(out, s_buf, (size_t)WATERFALL_WIDTH * WATERFALL_HEIGHT);
    return ESP_OK;
}

const uint16_t *waterfall_lut_rgb565(void)
{
    return s_lut;
}

uint32_t waterfall_column_count(void)
{
    return s_cols;
}

bool waterfall_is_ready(void)
{
    return s_ready;
}
