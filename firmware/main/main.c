/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "sdr_control.h"
#include "bsp_bringup.h"
#include "iq_ring.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32P4-RTL-SDR starting");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Phase 1: BSP bring-up pending");

    /*
     * SDR control: initialize the driver. If the USB fault guard has latched
     * (3 consecutive enumeration-time panics on prior boots, e.g. from a
     * flaky or unpowered dongle) the driver refuses usb_host_install and
     * returns ESP_RTL_SDR_ERR_USB_SAFE_MODE. Do NOT abort — log a clear
     * message and continue; display / audio still start and the user can
     * clear the guard after reconnecting a working dongle.
     */
    esp_err_t si = sdr_control_init();
    if (si == ESP_OK) {
        ESP_ERROR_CHECK(sdr_control_log_info());
        ESP_ERROR_CHECK(sdr_control_log_usb_bus());

        /*
         * Recovery for the warm-reset stale-device case: the board hard-ties
         * the OTG VBUS to VCC_5V, so a still-powered dongle survives a warm
         * MCU reset in a configured state and the host stack goes silent.
         * Ask any hub to drop and restore downstream power, then re-scan.
         * On a direct root port (no hub) this returns ESP_ERR_NOT_FOUND
         * and is harmless.
         */
        esp_err_t pc = sdr_control_hub_power_cycle(500);
        if (pc == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            ESP_ERROR_CHECK(sdr_control_log_usb_bus());
        }

        ESP_LOGI(TAG, "SDR control ready");
    } else if (si == ESP_RTL_SDR_ERR_USB_SAFE_MODE) {
        ESP_LOGE(TAG, "USB fault guard latched (3+ enumeration panics). "
                "Disconnect/reconnect the dongle, power-cycle the board, "
                "then call esp_rtl_sdr_usb_fault_guard_reset().");
    } else {
        ESP_LOGE(TAG, "sdr_control_init failed: %s (boot continues)",
                 esp_rtl_sdr_err_to_name(si));
    }

    /* IQ ring: PSRAM SPSC buffer for IQ blocks. 8 slots x 16 KiB = 128 KiB. */
    esp_err_t rr = iq_ring_alloc(IQ_RING_DEFAULT_SLOT_BYTES, IQ_RING_DEFAULT_SLOT_COUNT);
    if (rr != ESP_OK) {
        ESP_LOGE(TAG, "IQ ring alloc failed: %s", esp_err_to_name(rr));
    } else {
        ESP_LOGI(TAG, "IQ ring ready (%u KiB PSRAM)",
                 (unsigned)((IQ_RING_DEFAULT_SLOT_BYTES * IQ_RING_DEFAULT_SLOT_COUNT) / 1024u));
    }

    /*
     * BSP bring-up (task 1.7): display (LVGL + backlight), audio (I2S +
     * ES8311 speaker), and the BSP-managed USB host power path. Display is
     * required for M1; audio and USB host are opportunistic here — failures
     * log but do not abort the boot.
     */
    esp_err_t br = bsp_bringup_init();
    if (br != ESP_OK) {
        ESP_LOGE(TAG, "BSP bring-up failed: %s (boot continues)", esp_err_to_name(br));
    }
}
