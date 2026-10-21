/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "bsp_bringup.h"

#include "esp_log.h"
#include "esp_codec_dev.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"

#include "sdr_control.h"

static const char *TAG = "bsp_bring";

static bool             s_ready = false;
static lv_display_t    *s_disp = NULL;
static lv_indev_t      *s_indev = NULL;
static esp_codec_dev_handle_t s_speaker = NULL;

esp_err_t bsp_bringup_init(void)
{
    /* Display: creates the LVGL task + MIPI-DSI panel, turns on backlight. */
    s_disp = bsp_display_start();
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "display start failed (NULL LVGL handle)");
        return ESP_FAIL;
    }
    s_indev = bsp_display_get_input_dev();
    esp_err_t rt = bsp_display_backlight_on();
    if (rt != ESP_OK) {
        ESP_LOGW(TAG, "backlight on failed: %s", esp_err_to_name(rt));
    } else {
        ESP_LOGI(TAG, "display up: %dx%d LVGL, indev=%s",
                 BSP_LCD_H_RES, BSP_LCD_V_RES,
                 s_indev ? "yes" : "no");
    }

    /* Audio: init I2S + codec, grab the speaker handle. V1: 44.1 kHz mono
     * for bring-up; the DSP phase upgrades to 48 kHz stereo.
     * NULL passes the BSP's mono 22050 default — good enough for M1 smoke. */
    esp_err_t ar = bsp_audio_init(NULL);
    if (ar != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(ar));
    } else {
        s_speaker = bsp_audio_codec_speaker_init();
        if (s_speaker != NULL) {
            ESP_LOGI(TAG, "audio up: speaker codec ready");
        } else {
            ESP_LOGW(TAG, "speaker codec init returned NULL");
        }
    }

    /*
     * USB host: the esp_rtl_sdr driver (installed by sdr_control_init) already
     * owns the USB HS controller. bsp_usb_host_start() would call
     * usb_host_install() a second time → ESP_ERR_INVALID_STATE → abort.
     * The two are mutually exclusive on this single-USB-OTG-port board.
     * When the SDR driver is installed we skip the BSP's own USB host; when
     * it is not (safe-mode / fault-guard latched) the BSP path is still
     * available but not needed for M1.
     */
    if (!sdr_control_is_installed()) {
        esp_err_t ur = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, false);
        if (ur != ESP_OK) {
            ESP_LOGW(TAG, "usb host start failed: %s", esp_err_to_name(ur));
        } else {
            ESP_LOGI(TAG, "usb host up (bsp, no SDR driver)");
        }
    } else {
        ESP_LOGI(TAG, "usb host owned by esp_rtl_sdr driver; skipping bsp_usb_host_start");
    }

    s_ready = true;
    ESP_LOGI(TAG, "BSP bring-up complete");
    return ESP_OK;
}

void bsp_bringup_deinit(void)
{
    if (!s_ready) {
        return;
    }
    bsp_audio_deinit();
    s_speaker = NULL;
    /* Only stop the BSP's USB host if we started it (SDR not installed). */
    if (!sdr_control_is_installed()) {
        bsp_usb_host_stop();
    }
    s_disp  = NULL;
    s_indev = NULL;
    s_ready = false;
}

bool bsp_bringup_is_ready(void)
{
    return s_ready;
}

lv_display_t *bsp_bringup_lv_display(void)
{
    return s_disp;
}

lv_indev_t *bsp_bringup_lv_indev(void)
{
    return s_indev;
}
