# CONVENTIONS — ESP32P4-RTL-SDR

> Applies to all source code under `firmware/`.
> Language: C (C11, no C++).
> Coding style: ESP-IDF / Espressif style (4-space indent, K&R braces).

---

## 1. Directory Structure

```
ESP32P4-RTL-SDR/
├── docs/                         # This file + ARCHITECTURE.md + PRD.md + TECH_STACK.md
│   ├── ARCHITECTURE.md
│   ├── CONVENTIONS.md
│   ├── PRD.md
│   └── TECH_STACK.md
│
└── firmware/                    # ESP-IDF project root
    ├── CMakeLists.txt           # Top-level (boilerplate, 5 lines)
    ├── sdkconfig.defaults       # IDF 6.1.0 P4 defaults (do NOT commit sdkconfig)
    ├── sdkconfig.local          # .gitignore; local overrides (flash port, etc.)
    ├── partitions.csv           # Custom partition table
    │
    └── main/
        ├── CMakeLists.txt       # idf_component_register(...)
        ├── idf_component.yml    # Component registry dependencies
        │
        ├── main.c               # app_main: boot sequence
        │
        ├── sdr_control.c        # esp_rtl_sdr lifecycle wrapper
        ├── sdr_control.h
        │
        ├── dsp/
        │   ├── CMakeLists.txt
        │   ├── iq_ring.c        # Lock-free SPSC IQ ring buffer
        │   ├── iq_ring.h
        │   ├── fft.c            # esp-dsp FFT wrapper
        │   ├── fft.h
        │   ├── spectrum.c       # DC offset + Hann + FFT + 512-bin power
        │   ├── spectrum.h
        │   ├── waterfall.c      # Column generation + colormap
        │   ├── waterfall.h
        │   └── demod_fm.c       # Quadrature demod + decimation + LPF
        │       demod_fm.h
        │
        ├── audio/
        │   ├── CMakeLists.txt
        │   ├── audio_engine.c   # I2S TX + ES8311 + PCM ring
        │   ├── audio_engine.h
        │   └── pcm_ring.c       # Lock-free PCM SPSC ring
        │       pcm_ring.h
        │
        ├── ui/
        │   ├── CMakeLists.txt
        │   ├── ui_main.c        # Dashboard registry + tab switch + app task
        │   ├── ui_main.h
        │   ├── scope_dashboard.c # Scope mode: spectrum + waterfall + VFO + controls
        │   ├── scope_dashboard.h
        │   ├── fm_dashboard.c   # FM radio mode (Phase 4)
        │   ├── fm_dashboard.h
        │   ├── settings_dashboard.c
        │   ├── settings_dashboard.h
        │   ├── spectrum_view.c  # lv_canvas drawing for spectrum
        │   ├── spectrum_view.h
        │   ├── waterfall_view.c # lv_image drawing for waterfall
        │   ├── waterfall_view.h
        │   ├── vfo_ctrl.c       # VFO frequency display + tap-tune + keypad
        │   ├── vfo_ctrl.h
        │   └── style.c          # Color themes, fonts, object styles
        │       style.h
        │
        └── test/
            └── test_sdr_smoke.c # Minimal hardware smoke test (P0-1, P0-2)
```

---

## 2. File & Function Naming

| Element | Convention | Example |
|---------|-----------|---------|
| C source file | `snake_case.c` | `sdr_control.c`, `spectrum.c` |
| C header file | `snake_case.h` (matches .c) | `sdr_control.h` |
| Public function | `module_action()` (snake_case) | `sdr_control_start()`, `spectrum_compute()` |
| Private/static function | same as public, file-scoped | `static void waterfall_push_column(...)` |
| Struct type | `module_t` (trailing `_t`) | `sdr_ctrl_t`, `spectrum_state_t` |
| Constant / macro | `MODULE_CONST_NAME` (ALL CAPS) | `SPECTRUM_BINS`, `WATERFALL_COLS` |
| FreeRTOS task entry | `module_task()` | `sdr_dsp_task()`, `audio_out_task()` |
| Event callback | `on_<source>_<event>()` | `on_sdr_event()`, `on_lvgl_indev()` |
| Init function | `module_init()` | `dsp_init()`, `ui_init()` |
| Cleanup function | `module_deinit()` | `dsp_deinit()` |

---

## 3. CMakeLists.txt Patterns

### 3.1 `firmware/CMakeLists.txt` (top-level, 5-line boilerplate)

```cmake
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
```

### 3.2 `firmware/main/CMakeLists.txt`

```cmake
# List all source files explicitly (no glob — matches OrcSDR pattern)
idf_component_register(
  SRCS
    "main.c"
    "sdr_control.c"
    "dsp/iq_ring.c"
    "dsp/fft.c"
    "dsp/spectrum.c"
    "dsp/waterfall.c"
    "dsp/demod_fm.c"
    "audio/pcm_ring.c"
    "audio/audio_engine.c"
    "ui/ui_main.c"
    "ui/scope_dashboard.c"
    "ui/fm_dashboard.c"
    "ui/settings_dashboard.c"
    "ui/spectrum_view.c"
    "ui/waterfall_view.c"
    "ui/vfo_ctrl.c"
    "ui/style.c"
  INCLUDE_DIRS
    "."
    "dsp"
    "audio"
    "ui"
  REQUIRES
    esp_rtl_sdr
    bsp            # waveshare BSP component name in managed_components
    lvgl__lvgl
    esp-dsp
    esp_driver_i2s
    esp_driver_i2c
    esp_driver_gpio
    esp_driver_ledc
    nvs_flash
    esp_event
    esp_timer
    freertos
)

# Optional compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  SDR_FIRMWARE_VERSION=\"0.1.0\"
)
```

> **Component name notes:**
> - `esp_rtl_sdr` — folder name of the managed component (from `hardcoreerik/esp-rtl-sdr`)
> - `bsp` — the Waveshare BSP component registers as `bsp` in `REQUIRES`
> - `lvgl__lvgl` — LVGL managed component (double-underscore separator)
> - `esp-dsp` — Espressif DSP library
> - `esp_driver_i2s` / `esp_driver_i2c` / `esp_driver_gpio` — IDF native drivers

---

## 4. Error Handling

### 4.1 `esp_err_t` return values

```c
// Standard ESP-IDF pattern: return early on error, log the failure
esp_err_t ret = some_driver_call(...);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "some_driver_call failed: %s", esp_err_to_name(ret));
    return ret;
}
```

Or use the macro for single-call init sequences:
```c
ESP_RETURN_ON_ERROR(some_driver_call(...), TAG, "some_driver_call failed");
ESP_RETURN_ON_ERROR(another_call(...),     TAG, "another_call failed");
```

### 4.2 `NULL` pointer checks

```c
if (!handle) {
    ESP_LOGE(TAG, "handle is NULL");
    return ESP_ERR_INVALID_STATE;
}
```

### 4.3 No bare `assert()` in production code

Use `ESP_RETURN_ON_FALSE(cond, ERR_CODE, TAG, "msg")` instead.
`assert()` is acceptable only in `test/` directory and debug builds.

---

## 5. Logging

### 5.1 One TAG per file

```c
// At top of each .c file:
static const char *TAG = "sdr_ctrl";   // max 10 chars, no spaces
```

| File | TAG |
|------|-----|
| `main.c` | `"main"` |
| `sdr_control.c` | `"sdr_ctrl"` |
| `dsp/spectrum.c` | `"dsp_spec"` |
| `dsp/waterfall.c` | `"dsp_wf"` |
| `dsp/demod_fm.c` | `"dsp_fm"` |
| `audio/audio_engine.c` | `"audio"` |
| `ui/ui_main.c` | `"ui"` |
| `ui/scope_dashboard.c` | `"ui_scope"` |

### 5.2 Log levels

| Level | Use |
|-------|-----|
| `ESP_LOGE` | Error, unrecoverable or degraded operation |
| `ESP_LOGW` | Warning, degraded but functional |
| `ESP_LOGI` | Informational, state transitions, boot sequence |
| `ESP_LOGD` | Debug only (compile-time disabled by default) |
| `ESP_LOGV` | Verbose, per-frame / per-sample data (off in production) |

### 5.3 Log rate limiting

For periodic logs (metrics, DSP stats), use `esp_timer` with `ESP_LOGI` at 1 Hz minimum.
Never log inside tight per-sample loops.

---

## 6. Memory Allocation

### 6.1 PSRAM (large, static, non-critical buffers)

```c
#include "esp_heap_caps.h"

uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed: %u bytes", size);
    return ESP_ERR_NO_MEM;
}
```

Use for: IQ ring, spectrum arrays, waterfall buffer, PCM ring, LVGL draw buffer.

### 6.2 SRAM (hot, small, latency-critical)

```c
// Internal SRAM (fast, small)
uint16_t *window = heap_caps_malloc(SPECTRUM_FFT_SIZE * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL);
```

Use for: Hann window table, FFT twiddle factors, small FIFO, task stacks.

### 6.3 No `malloc()` / `free()` without `heap_caps` prefix

Always specify the memory pool explicitly. Bare `malloc()` uses the default (SRAM) pool
and can fragment; use `heap_caps_malloc()` / `heap_caps_free()` consistently.

---

## 7. FreeRTOS Task Creation

### 7.1 Pattern

```c
void xTaskCreatePinnedToCore(
    sdr_dsp_task,          // task function
    "sdr_dsp",             // name (for monitoring)
    16384,                 // stack (bytes, must be multiple of 8)
    NULL,                  // parameter
    12,                    // priority (0 = idle, 24 = max)
    &sdr_dsp_task_handle,  // handle
    0                      // pinned to Core 0
);
```

### 7.2 Task priority table

| Task | Priority | Core | Stack |
|------|----------|------|-------|
| `usb_host` (driver internal) | ~24 | 0 | 8 K |
| `sdr_dsp` | 12 | 0 | 16 K |
| `sdr_fm` | 11 | 0 | 12 K |
| `audio_out` | 10 | 1 | 8 K |
| `lvgl_task` | 5 | 1 | 16 K |
| `lvgl_indev` | 5 | 1 | 8 K |
| `app_main` | 5 | 0 | 12 K |

### 7.3 Task exit / cleanup

Tasks that run forever: `while (true) { ... vTaskDelay(...); }`
Shutdown tasks: use `TaskHandle_t` + `xTaskNotify` to signal exit; check `ulTaskNotifyTake()`.

---

## 8. sdkconfig Management

### 8.1 Files

| File | Purpose | Committed? |
|------|---------|-----------|
| `sdkconfig.defaults` | Base config for ESP32-P4 (check in) | **Yes** |
| `sdkconfig.defaults.esp32p4` | P4-specific overrides (check in) | **Yes** |
| `sdkconfig` | Generated by `idf.py`; **never** check in | **No** |
| `sdkconfig.local` | Local machine-specific (COM port, flash path) | **No** (`.gitignore`) |

### 8.2 `sdkconfig.defaults` content (to be created in Phase 1)

```ini
# Target
CONFIG_IDF_TARGET="esp32p4"

# Flash: 32 MB QIO
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y

# PSRAM: 32 MB Octal
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y

# Optimisation
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288

# USB Host
CONFIG_USBH_HOST_CONTROL_TRANSFER_MAX_PACKET_SIZE=4096

# LVGL
CONFIG_LV_DEF_REFR_PERIOD=15
CONFIG_LV_OS_FRERTOS=y
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2
CONFIG_LV_USE_FONT_COMPRESSED=y
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_LV_FONT_MONTSERRAT_32=y
CONFIG_LV_FONT_MONTSERRAT_40=y

# Custom partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Log level
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# Coredump (for crash analysis)
CONFIG_ESP_COREDUMP_ENABLE_TO_SECOND_FLASH=y
```

---

## 9. NVS (Settings Persistence)

### 9.1 Namespace

| Key | Type | Description |
|-----|------|-------------|
| `nvs` namespace | — | Main app namespace |
| `last_freq_mhz` | `float` | Last tuned frequency |
| `last_gain` | `u8` | Last manual gain (0–49) |
| `last_volume` | `u8` | Last volume (0–100) |
| `ppm` | `i16` | PPM correction (−200 to +200) |
| `preset_N` (N=1..8) | `float` | Preset frequency |
| `preset_N_label` | `char[32]` | Preset label |

### 9.2 Pattern

```c
nvs_handle_t nvs;
nvs_open("sdr86", NVS_READWRITE, &nvs);

// Write
nvs_set_float(nvs, "last_freq_mhz", freq_mhz);
nvs_commit(nvs);

// Read (with default)
float freq = 96.1f;
nvs_get_float(nvs, "last_freq_mhz", &freq);  // returns ESP_OK or ESP_ERR_NVS_NOT_FOUND
nvs_close(nvs);
```

---

## 10. Git Conventions

### 10.1 Branch naming

```
main
develop
feature/<short-desc>    e.g. feature/spectrum-dsp
fix/<issue-short-desc>  e.g. fix/usb-hs-enumeration
```

### 10.2 Commit message format

```
<type>: <short description (imperative, <= 72 chars)>

[optional body]
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`

Examples:
```
feat: add IQ ring buffer and DC offset removal
fix: ES8311 MCLK not configured for 48 kHz sample rate
docs: update TECH_STACK.md with GPIO mapping for Smart 86
refactor: extract waterfall colormap LUT into waterfall.c
test: add smoke test for esp_rtl_sdr stream start
chore: pin lvgl to 9.2.0 in idf_component.yml
```

### 10.3 Do not commit

- `firmware/sdkconfig` (generated)
- `firmware/build/` (build artifacts)
- `firmware/managed_components/` (fetched by idf-component-manager)
- `firmware/.vscode/` (IDE settings)
- `*.o`, `*.a`, `*.bin`, `*.elf`

---

## 11. Testing Strategy

### 11.1 Host-side unit tests (DSP math)

Run on PC (x86/ARM) using `esp-dsp` host target:

```
test/
├── test_spectrum.c      # Verify FFT output against known input
├── test_waterfall.c     # Verify colormap LUT boundary values
└── test_demod_fm.c      # Verify demod phase on synthetic FM signal
```

Use `ESP_UNIT_TEST` framework (comes with IDF).

### 11.2 Hardware smoke test

`test/test_sdr_smoke.c`:
- Start `esp_rtl_sdr` stream at 960 KSPS
- Wait 5 s
- Log `effective_sps`, `overruns`, `consumer_drops`
- Assert `effective_sps > 900K` and `overruns == 0`

### 11.3 Display / audio verification

- Serial log: confirm `EVT_START`, no `EVT_ERROR`
- Visual: spectrum and waterfall visible on 720×720 display
- Audio: known FM station audible through speaker at 107.5 MHz

---

## 12. License Header

All source files:

```c
/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */
```

(Use MIT for original code; note that `esp-rtl-sdr` is AGPL-3.0-only —
the two license domains are separate; document in README.)

---

## 13. Phase 1 Build Checklist

Before starting Phase 1 code, verify:

- [ ] `C:\esp\v6.1\esp-idf` exists and `idf.py --version` reports 6.1.0
- [ ] RISC-V P4 toolchain installed (`idf.py set-target esp32p4` succeeds without errors)
- [ ] Python venv at `C:\esp\v6.1\` has all IDF dependencies
- [ ] `firmware/CMakeLists.txt` top-level boilerplate is correct
- [ ] `firmware/main/idf_component.yml` has all 5 dependencies listed
- [ ] `firmware/sdkconfig.defaults` created with P4 settings
- [ ] `firmware/.gitignore` includes `build/`, `sdkconfig`, `managed_components/`
- [ ] `docs/` folder present with all 4 planning documents
