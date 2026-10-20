# TECH_STACK — ESP32P4-RTL-SDR

> Firmware project: `firmware/`
> Build tool: ESP-IDF 6.1.0 at `C:\esp\v6.1\esp-idf`
> Target chip: ESP32-P4 (RISC-V dual-core @ 360 MHz)

---

## 1. Hardware BOM

### 1.1 Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box)

| Component | Spec |
|-----------|------|
| MCU | ESP32-P4NRW32 (RISC-V dual-core @ 360 MHz + LP RISC-V @ 40 MHz) |
| PSRAM | 32 MB MQPS64 (octal, in-package) |
| Flash | 32 MB NOR (QSPI) |
| Display | 4-inch IPS 720 × 720, MIPI DSI 2-lane, ST7703 driver |
| Touch | GT911 5-point capacitive, I2C (poll mode — no INT pin to GPIO) |
| Audio codec (DAC) | ES8311 (I2C + I2S), NS4150B class-D amp, 8 Ω 2 W speaker |
| Audio codec (ADC) | ES7210 dual-mic, I2C + I2S/TDM (not used in V1) |
| USB OTG | USB 2.0 HS OTG (USBD_P / USBD_N, P4 fixed pins) |
| UART | CH343P USB-UART (GPIO37 RX, GPIO38 TX) for flashing/debug |
| Ethernet | IP101 10/100M RMII (not used in V1) |
| Wi-Fi / BT | ESP32-C6 SDIO coprocessor (deferred) |
| TF card | SDIO 3.0 4-bit (deferred) |
| RTC battery | Rechargeable RTC battery header |
| Power | DC 6–30 V input (86 Panel version) / 5 V USB (standard 4B) |

### 1.2 RTL-SDR Blog V3

| Spec | Value |
|------|-------|
| Chip | RTL2832U |
| Tuner | R820T2 |
| RF range | 24 MHz – 1.766 GHz (normal); Q-branch direct < 24 MHz (experimental) |
| Sample rates | 225 001 – 3 000 000 SPS |
| Data format | CU8 interleaved I/Q (uint8 × 2, 0–255, 128 = 0) |
| Gain | 0 – 49 dB manual ladder; AGC AUTO |
| Crystal PPM | ±200 ppm correction (software) |
| USB | USB 2.0 full/high-speed device; bulk IN 0x81, bulk OUT 0x01 |
| Vendor:Product | 0x16D5:0x093A (R820T2) |

---

## 2. Firmware Stack Layers

```
┌──────────────────────────────────────────────────────────────────┐
│  Application layer (firmware/main/)                             │
│  sdr_control.c   UI dashboards   DSP pipeline   audio_engine    │
├──────────────────────────────────────────────────────────────────┤
│  ESP Component Registry (managed_components/)                   │
│  waveshare/esp32_p4_wifi6_touch_lcd_4b   v3.0.1                │
│  hardcoreerik/esp-rtl-sdr                v0.9.3                │
│  lvgl/lvgl                                 v9.2.0              │
│  espressif/esp-dsp                         (latest 1.x)        │
├──────────────────────────────────────────────────────────────────┤
│  ESP-IDF 6.1.0 native components                                │
│  esp_driver_i2s  esp_driver_i2c  esp_driver_gpio  esp_driver_  │
│  esp_driver_ledc  nvs_flash  esp_event  esp_timer  freertos    │
├──────────────────────────────────────────────────────────────────┤
│  Hardware: ESP32-P4 + Waveshare Smart 86 + RTL-SDR V3          │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. Component Registry Entries

### 3.1 `main/idf_component.yml` (to be created)

```yaml
dependencies:
  idf:
    version: ">=6.1.0"

  # Waveshare Smart 86 BSP (display + touch + audio codec + backlight)
  waveshare/esp32_p4_wifi6_touch_lcd_4b:
    version: ">=3.0.1"

  # RTL-SDR USB Host driver (esp_rtl_sdr)
  hardcoreerik/esp-rtl-sdr:
    version: ">=0.9.3"
    repo_url: "https://github.com/hardcoreerik/esp-rtl-sdr"

  # LVGL v9 UI framework (pinned; comes with BSP but pin explicitly)
  lvgl/lvgl:
    version: ">=9.2.0"

  # Espressif DSP library (FFT, FIR, IIR, window functions)
  espressif/esp-dsp:
    version: ">=1.3.0"

  # ES8311 I2S codec driver (ESP-IDF native, explicit for clarity)
  # Comes via waveshare BSP; listed for documentation
  # espressif/es8311:  # transitive via BSP
```

> **Note:** `esp-rtl-sdr` v0.9.3 declares `idf >= 5.5.0` and `targets: esp32p4`.
> It should work on IDF 6.1.0 (forward compatible). Verify in Phase 1 task 1.1.

---

## 4. Key API References

### 4.1 esp_rtl_sdr (v0.9.3)

**Lifecycle:**
```c
esp_rtl_sdr_config_t cfg;
esp_rtl_sdr_config_default(&cfg);
cfg.delivery_mode = ESP_RTL_SDR_DELIVERY_CALLBACK;
cfg.event_cb      = on_sdr_event;
cfg.usb_task_core_id = 0;

esp_rtl_sdr_handle_t sdr;
esp_rtl_sdr_install(&cfg, &sdr);            // IDLE
esp_rtl_sdr_start_hz(sdr, 96100000, 960000); // → STARTING → STREAMING
esp_rtl_sdr_stop(sdr, 0);                  // → IDLE
esp_rtl_sdr_uninstall(sdr);                 // destroy handle
```

**Tuning & Control:**
```c
esp_rtl_sdr_retune_hz(sdr, 107500000, 0);   // sync from app task
esp_rtl_sdr_set_gain(sdr, 49);               // manual gain 0-49 dB
esp_rtl_sdr_set_ppm(sdr, 0);                // crystal PPM ±200
esp_rtl_sdr_apply_need(sdr, ESP_RTL_SDR_NEED_FM);  // preset: 960K, 96.1 MHz
```

**Events (callback context — must return fast, < 1 ms):**
```c
void on_sdr_event(esp_rtl_sdr_event_t ev, const void *payload, void *ctx) {
    if (ev == ESP_RTL_SDR_EVT_IQ_BLOCK) {
        const esp_rtl_sdr_iq_block_t *iq = payload;
        // iq->data: CU8, iq->size bytes
        // Copy to PSRAM ring; call esp_rtl_sdr_release_iq_block(ctx)
    }
    if (ev == ESP_RTL_SDR_EVT_RETUNED) { /* LO confirmed applied */ }
    if (ev == ESP_RTL_SDR_EVT_START)     { /* streaming confirmed */ }
    if (ev == ESP_RTL_SDR_EVT_ERROR)     { /* log + trigger recovery */ }
}
```

**Metrics (call from app task, periodic):**
```c
esp_rtl_sdr_metrics_t m;
esp_rtl_sdr_get_metrics(sdr, &m);
// m.effective_sps, m.overruns, m.consumer_drops, m.last_error
```

**Sample rate macros:**
```c
ESP_RTL_SDR_RATE_960K   // 960 000 SPS  — FM class
ESP_RTL_SDR_RATE_2048K  // 2 048 000 SPS — ADS-B class
// Valid range: 225 001 – 3 000 000 Hz (per driver allowlist)
```

### 4.2 Waveshare BSP (esp32_p4_wifi6_touch_lcd_4b v3.0.1)

**Display (LVGL + MIPI DSI):**
```c
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

bsp_display_cfg_t cfg = {
    .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size    = BSP_LCD_DRAW_BUFF_SIZE,
    .double_buffer  = BSP_LCD_DRAW_BUFF_DOUBLE,
    .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = false }
};
lv_display_t *disp = bsp_display_start_with_config(&cfg);
bsp_display_backlight_on();
// Optional: bsp_display_brightness_set(80);  // 0-100%
```

**Audio (ES8311 speaker out):**
```c
#include "esp_codec_dev.h"
#include "bsp/audio_codec.h"

// BSP handles I2C + I2S + ES8311 init internally:
bsp_audio_codec_speaker_init();          // returns audio_codec_handle_t
esp_codec_dev_set_in_gain(h, 0);
esp_codec_dev_set_out_vol(h, 50);        // 0-100
```

**Touch (GT911 poll — no INT pin):**
```c
// BSP configures GT911 in poll mode automatically.
// LTGL indev callback registered by bsp_display_start_with_config().
// No additional code needed for basic touch.
```

### 4.3 LVGL v9 Widgets (for spectrum / waterfall / controls)

| Widget | Use | Key API |
|--------|-----|---------|
| `lv_canvas` | Spectrum chart (PSRAM RGB565, 720×200) | `lv_canvas_set_buffers`, `lv_canvas_draw_line` |
| `lv_image` | Waterfall (PSRAM palette-indexed, 720×240) | `lv_image_set_src`, `lv_image_set_area` |
| `lv_label` | VFO frequency display | `lv_label_set_text` |
| `lv_slider` | Gain / Volume / PPM | `lv_slider_set_value` |
| `lv_button` | Presets / Scan / Mark | `lv_obj_add_event_cb` |
| `lv_switch` | AGC toggle | `lv_switch_set_state` |
| `lv_tabview` | Dashboard tabs | `lv_tabview_set_tab` |

### 4.4 esp-dsp (Espressif DSP library)

| Function | Use | Notes |
|----------|-----|-------|
| `dsp_cfft_f32` / `cfft_f32` | 1024-pt complex FFT | In-place, PSRAM buffer; pre-computed twiddle |
| `dsp_fir_decim_f32` | FIR decimation (2:1, 10:1) | For FM audio path |
| `dsp_fir_f32` | FIR LPF 15 kHz | Audio low-pass |
| `dsp_iir_f32` | IIR Biquad LPF / HPF | Alternatives to FIR |
| `dsp_window_hann` | Hann window pre-compute | 1024-tap, int16-scaled |

---

## 5. GPIO Mapping (Smart 86 Box)

Source: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B documentation.

| Signal | GPIO | Device / Function |
|--------|------|-------------------|
| I2C_SDA | GPIO7 | GT911, ES8311, ES7210, camera I2C (shared bus, 100 kHz) |
| I2C_SCL | GPIO8 | Same as above |
| I2S_MCK | GPIO13 | ES8311 MCLK (12.288 MHz for 48 kHz) |
| I2S_BCK | GPIO12 | ES8311 bit clock |
| I2S_WS  | GPIO10 | ES8311 LRCK / ES7210 SDOUT1 |
| I2S_DO  | GPIO9  | ES8311 data in (P4 → codec) |
| I2S_DI  | GPIO11 | ES7210 data out (codec → P4) |
| PA_CTRL | GPIO53 | NS4150B speaker amp enable (high = on) |
| TP_RST  | GPIO23 | GT911 touch reset (driven low at boot, then high) |
| BL_EN   | GPIO33 | LCD backlight boost enable (high = on) |
| BL_PWM  | GPIO26 | LCD backlight PWM (LEDC, 0–100%) |
| LCD_RST | GPIO27 | MIPI DSI LCD reset |
| USB_HS  | USBD_P / USBD_N | RTL-SDR V3 dongle connection |
| UART_RX | GPIO37 | CH343P RX (flashing / debug) |
| UART_TX | GPIO38 | CH343P TX |
| TF_D0–D3| GPIO39–42 | SDIO 4-bit TF card (deferred) |
| TF_CLK  | GPIO43 | SDIO clock (deferred) |
| TF_CMD  | GPIO44 | SDIO command (deferred) |
| TF_PWR  | GPIO45 | SD1_VDD power control (deferred) |
| ETH_*   | GPIO28–31, 34/35, 49–52 | IP101 RMII (not used V1) |
| RELAY1  | GPIO32 | 86 Panel bottom board relay (not used V1) |
| RELAY2  | GPIO46 | 86 Panel bottom board relay (not used V1) |
| RS485_TX| GPIO47 | RS485 transceiver TXD (not used V1) |
| RS485_RX| GPIO48 | RS485 transceiver RXD (not used V1) |
| C6_IO   | GPIO6/14/16/18–22/54 | ESP32-C6 SDIO coprocessor (deferred) |

---

## 6. Sample Rate & Frequency Constraints

From `esp-rtl-sdr` v0.9.3 `docs/RATES.md`:

| Use case | Sample rate | Macro | LO range |
|----------|------------|-------|----------|
| FM broadcast | 960 000 SPS | `ESP_RTL_SDR_RATE_960K` | 24 MHz – 1.766 GHz |
| ADS-B / Mode-S | 2 048 000 SPS | `ESP_RTL_SDR_RATE_2048K` | 24 MHz – 1.766 GHz |
| HF (below 24 MHz) | 225 001 – 960 000 SPS | — | Q-branch direct; gain bypassed |

**Driver allowlist:** valid SPS values are a discrete list. Use `esp_rtl_sdr_quantize_sample_rate()` to find the closest supported rate. Do not assume any arbitrary SPS is valid.

**RTL-SDR V3 gain note:** R820T2 tuner gain range is 0–49 dB (manual ladder).
In Q-branch direct mode (< 24 MHz), tuner gain is bypassed; RTL2832u digital IF gain is 0–70 dB.

---

## 7. Build Environment

| Item | Value |
|------|-------|
| ESP-IDF version | 6.1.0 |
| ESP-IDF path | `C:\esp\v6.1\esp-idf` |
| Target chip | `esp32p4` |
| Build command | `idf.py -D IDF_TARGET=esp32p4 fullclean build` |
| Flash port | CH343P UART (GPIO37 RX, GPIO38 TX) — **not** USB OTG |
| Flash command | `idf.py -p COMx flash monitor` |
| Python venv | `C:\esp\v6.1\` (ESP-IDF Python virtual environment) |
| Toolchain | RISC-V 32-bit (XTENSA/RISC-V cross-compiler, installed with IDF 6.1.0) |
| C standard | C11 (default for ESP-IDF) |
| CMake minimum | 3.22 |

### Flash Layout (32 MB NOR)

```
0x000000  bootloader.bin
0x00C000  partition-table.bin
0x010000  nvs.bin          (data, 0x60000)
0x070000  storage.bin      (data/SPIFFS, 0xF10000)  [for assets, deferred]
0x1000000 factory.bin     (app, 0x1000000 = 16 MB)
```

*Adjust partition sizes once firmware size is known. 16 MB app partition is generous for V1.*

---

## 8. Reference Resources

| Resource | URL / Path |
|----------|-----------|
| Waveshare Smart 86 hardware docs | `https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B` |
| Waveshare BSP component (registry) | `https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_4b` |
| Waveshare BSP local reference examples | `C:\D\MY\DEV\ESP-IDF\ESP32-P4-86-PANEL-ESP-IDF` |
| esp-rtl-sdr driver (GitHub) | `https://github.com/hardcoreerik/esp-rtl-sdr` |
| esp-rtl-sdr API reference | `esp-rtl-sdr/docs/API_REFERENCE.md` |
| OrcSDR application (reference) | `https://github.com/hardcoreerik/OrcSDR` |
| ESP-IDF P4 examples (audio) | `C:\esp\v6.1\esp-idf\examples\audio\audio_i2s` |
| ESP-IDF P4 examples (USB host) | `C:\esp\v6.1\esp-idf\examples\usb\usb_host_bulk` |
| ESP-IDF P4 examples (LVGL) | `C:\esp\v6.1\esp-idf\examples\peripherals\lvgl` |
