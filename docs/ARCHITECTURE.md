# ARCHITECTURE — ESP32P4-RTL-SDR

> Project: ESP32P4-RTL-SDR
> Hardware: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box) + RTL-SDR Blog V3 (R820T2)
> Firmware: ESP-IDF 6.1.0, ESP32-P4 RISC-V dual-core @ 360 MHz
> Status: Planning

---

## 1. System Block Diagram

```
                    ┌──────────────────────────────────────────────────────┐
                    │          ESP32-P4  (Waveshare Smart 86 Box)          │
                    │                                                     │
  Antenna ──► RTL-SDR V3 ──── USB 2.0 HS ────► USBD_P / USBD_N           │
                    │                       │                             │
                    │                       ▼                             │
                    │           ┌─────────────────────────┐               │
                    │           │   esp_rtl_sdr driver     │               │
                    │           │   (USB Host, Core 0)     │               │
                    │           │   CU8 interleaved I/Q    │               │
                    │           │   960 KSPS / 2.048 MSPS  │               │
                    │           └────────────┬──────────────┘               │
                    │                        │                              │
                    │        ┌───────────────┴────────────────┐             │
                    │        ▼                                 ▼            │
                    │  Core 0 (DSP)                   Core 1 (UI + Audio)   │
                    │  ┌──────────────────────┐     ┌────────────────────┐  │
                    │  │ IQ Ring (PSRAM,       │     │ LVGL UI (720×720)  │  │
                    │  │ lock-free SPSC)       │────►│ Spectrum chart    │  │
                    │  │ DC offset removal     │     │ Waterfall image   │  │
                    │  │ Hann window + FFT     │     │ VFO display       │  │
                    │  │ Power spectrum 512bin │     │ FM radio panel    │  │
                    │  │ Peak-hold             │     │ Settings screen   │  │
                    │  │ Waterfall column gen  │     │                   │  │
                    │  │ FM demod (quad+decim) │     │ Audio Engine       │  │
                    │  │ 48 kHz PCM out        │────►│ I2S → ES8311 DAC  │  │
                    │  └──────────────────────┘     │ NS4150B PA → 8Ω   │  │
                    │                                └────────────────────┘  │
                    │                                                     │
                    │  PSRAM 32 MB (MQPS64)   SRAM ~512 KB + DTCM        │
                    └─────────────────────────────────────────────────────┘
```

### Data Flow (steady-state streaming)

```
esp_rtl_sdr USB bulk-IN  (16384 B × 6 transfers, Core 0)
   │
   ▼  EVT_IQ_BLOCK callback  (CU8: I0 Q0 I1 Q1 ... uint8_t)
   │
   │  Convert: i = data[2n] - 128, q = data[2n+1] - 128
   ▼
   IQ Ring Buffer  (PSRAM, 128 KiB, lock-free SPSC, 4 slots × 32 KiB)
   │
   ├──► Spectrum task (Core 0, prio 12)
   │        │ DC-offset remove → Hann window → FFT 1024 (esp-dsp, PSRAM scratch)
   │        │ |X|² → dBm scale → 512 bins → peak-hold (decay 0.1 dB/ms)
   │        ▼
   │     Spectrum double-buffer (int16 × 512, PSRAM, mutex or seq-count)
   │        │
   │        ▼  (30 Hz refresh, FreeRTOS event group)
   │     LVGL spectrum widget (Core 1)
   │
   ├──► Waterfall task (Core 0, folded into spectrum task, ~4 columns/s)
   │        │ max-per-2-bins → 720 px column → colormap LUT (inferno, 256×4 B)
   │        ▼
   │     Waterfall PSRAM buffer (720 × 240 × 1 B = 169 KiB, column-major)
   │        │
   │        ▼  (scroll every ~250 ms)
   │     LVGL image widget partial invalidation (Core 1)
   │
   └──► FM Demod task (Core 0, prio 11)
            │ Quadrature demodulator (angle difference) → decimate 2:1 FIR
            │ → decimate 10:1 FIR → 48 kHz → IIR LPF 15 kHz → soft-clip
            ▼
         PCM ring (PSRAM, 256 KiB, SPSC, 48 kHz stereo int16)
            │
            ▼  [I2S DMA, Core 1]
         i2s_channel_write() → I2S TX (48 kHz, 16-bit, stereo)
            │
            ▼
         ES8311 DAC → NS4150B PA (GPIO53 high) → 8 Ω 2 W speaker
```

---

## 2. Core Split & Task Architecture

ESP32-P4 has two RISC-V app cores (Core 0 and Core 1) @ 360 MHz.
Real-time USB + DSP is time-critical; UI and audio output are interrupt/DMA-driven.
Pin heavy numeric work to Core 0; render + I2S audio to Core 1.

| Task | Core | Priority | Stack | Role |
|------|------|----------|-------|------|
| `usb_host` (esp_rtl_sdr internal) | 0 | ~24 | 8 K | USB protocol, bulk URB completion → `EVT_IQ_BLOCK` |
| `sdr_dsp` | 0 | 12 | 16 K | Reads IQ ring; runs FFT + spectrum + waterfall; calls `release_iq_block()` |
| `sdr_fm` | 0 | 11 | 12 K | FM demod → PCM ring write |
| `audio_out` | 1 | 10 | 8 K | Reads PCM ring → `i2s_channel_write()` to ES8311 |
| `lvgl_task` | 1 | 5 | 16 K | LVGL render loop (`lv_task_handler`), 30–60 fps |
| `lvgl_indev` | 1 | 5 | 8 K | Polls GT911 (I2C, no INT pin) → `lv_indev_report`, 60 Hz |
| `app_main` | 0 | 5 | 12 K | Boot sequence only; after task creation, idle |

### Inter-task synchronization

| Buffer | Producer | Consumer | Mechanism |
|--------|----------|----------|-----------|
| IQ ring (128 KiB PSRAM) | `usb_host` callback | `sdr_dsp` | Lock-free SPSC, atomic head/tail (RISC-V `ld.aq`/`st.rl`) |
| Spectrum array (2 × 512 × int16) | `sdr_dsp` | LVGL | `mutex_t` (held < 20 µs) or double-buffer + sequence counter |
| Waterfall column | `sdr_dsp` | LVGL | Event group notify; LVGL redraws only new column region |
| PCM ring (256 KiB PSRAM) | `sdr_fm` | `audio_out` | SPSC ring; I2S DMA 2 ms blocks; silent-fill on underrun |
| State changes (freq/gain/stop) | LVGL buttons → app task | `esp_rtl_sdr` API | Sync call from app task; from callback: async queued only |

---

## 3. esp_rtl_sdr Lifecycle

From `esp_rtl_sdr` v0.9.3 (`docs/API_REFERENCE.md`):

```
UNINSTALLED ──install()──► IDLE ──start_hz()──► STARTING ──► STREAMING
                             ▲                                      │
                             │                                      │
                             └────────────── stop() ◄───────────────┘
                                       or FAULT
                                         │
                                         └── reset() / uninstall()
```

### API calls used in firmware

| Call | Context | Notes |
|------|---------|-------|
| `esp_rtl_sdr_install(&cfg, &h)` | `app_main` | `delivery_mode = CALLBACK`, `event_cb = on_sdr_event`, `usb_task_core_id = 0` |
| `esp_rtl_sdr_get_capabilities()` | `app_main` | Verify `CAP_STREAM`, `CAP_GAIN`, `CAP_DIRECT_SAMPLING` (V3 Q-branch < 24 MHz) |
| `esp_rtl_sdr_start_hz(h, 96100000, 960000)` | `app_main` | LO 96.1 MHz (KZEL preset), 960 KSPS |
| `on_sdr_event(EVT_IQ_BLOCK, &iq, ctx)` | `usb_host` task | Copy CU8 to PSRAM ring; call `esp_rtl_sdr_release_iq_block(ctx)`; must return < 1 ms |
| `esp_rtl_sdr_retune_hz(h, new_hz, 0)` | `sdr_dsp` or app task | Sync from app task; async queued from callback context |
| `esp_rtl_sdr_set_gain(h, 49)` | app task | Manual 0–49 dB for R820T2 V3; AGC off |
| `esp_rtl_sdr_set_ppm(h, ppm)` | app task | ±200 ppm crystal correction |
| `esp_rtl_sdr_get_metrics(h, &m)` | app task (periodic) | Monitor `effective_sps`, `overruns`, `consumer_drops`, `last_error` |
| `esp_rtl_sdr_stop(h, 0)` | app task | Stop streaming |
| `esp_rtl_sdr_uninstall(h)` | on app exit | Release handle |

### RTL-SDR V3 specific notes (R820T2)

- **Tuner gain:** 0–49 dB manual ladder; AGC AUTO also available.
- **Standard range:** 24 MHz–1.766 GHz (R820T2 normal tuner path).
- **Below 24 MHz:** Q-branch direct sampling (`CAP_DIRECT_SAMPLING`). R820T2 band-select bypassed; gain in RTL2832u digital IF (0–70 dB). Sensitivity degrades.
- **No HF upconverter** on V3 (unlike V4). Below ~25 MHz use direct-Q; document in UI.

---

## 4. DSP Pipeline Detail

### 4.1 Spectrum (Core 0, `sdr_dsp` task)

```
Per IQ block (16384 CU8 bytes = 8192 complex samples @ 960 KSPS ≈ 8.5 ms):
  1. Convert CU8 → int16 (i - 128, q - 128)
  2. DC offset removal: subtract running 1024-sample mean (per channel)
  3. Hann window (pre-computed int16-scaled table, in SRAM)
  4. FFT 1024 (esp_dsp_cfft_f32, float32 complex, PSRAM scratch)
     8192 / 1024 = 8 FFT frames per block (75% overlap, 256-step)
  5. Power: P[k] = (|X[k]|² / N) × scale → dBm relative
  6. Downsample: average pairs → 512 bins
  7. Peak-hold: peak[k] = max(peak[k], P[k]); decay 0.1 dB/ms
  8. Push to spectrum double-buffer (int16[512])
```

**Performance estimate:**
`esp_dsp_cfft_f32` 1024-point ≈ 40–60 µs on P4 @ 360 MHz.
8 frames × 60 µs = 480 µs per 8192-sample block.
At 960 KSPS, block interval = 8.5 ms → DSP duty cycle ≈ 6%. Plenty of headroom.

### 4.2 Waterfall (Core 0, sub-task or folded into spectrum task)

```
Per spectrum frame (~33 ms):
  1. max-of-2-bins → 720 px column value (int16 → uint8 after min-max normalize)
  2. Color-map: 256-entry inferno LUT → palette index
  3. Push column to PSRAM ring (720 wide × 240 tall, column-major)
  4. Signal event group → LVGL redraws new column only
```

Waterfall PSRAM: 720 �� 240 × 1 B = 169 KiB. LUT: 256 × 4 B = 1 KiB.

### 4.3 FM Demodulation (Core 0, `sdr_fm` task)

```
960 KSPS complex I/Q →
  FIR decimate 2:1 (32-tap, esp_dsp) → 480 KSPS real baseband →
  Quadrature demodulator (angle difference / atan2) → 480 kHz FM audio →
  FIR decimate 10:1 (128-tap, esp_dsp) → 48 kHz →
  IIR LPF 15 kHz (64-tap) → soft-clip (tanh) → int16 PCM
```

V1: mono output. Stereo demod (BPF ±23 kHz + Hilbert) deferred to Phase 5.

### 4.4 Audio Output (Core 1, `audio_out` task)

```
PCM ring (PSRAM, 256 KiB ≈ 27 ms @ 48 kHz stereo int16) →
  i2s_channel_write() → I2S TX (48 kHz, 16-bit, stereo, MCLK = 12.288 MHz) →
  ES8311 DAC → NS4150B PA (GPIO53) → 8 Ω 2 W speaker
```

ES8311 configuration (from BSP reference `06_I2SCodec`):

| Parameter | Value |
|-----------|-------|
| MCLK | 12 288 000 Hz (256 × 48 k) |
| Sample rate | 48 000 Hz |
| Data width | 16-bit |
| Channels | Stereo (I2S) |
| Voice volume | 0–100 (`es8311_voice_volume_set`) |
| I2C bus | Shared with GT911: SDA=GPIO7, SCL=GPIO8, 100 kHz |
| I2S pins | MCK=GPIO13, BCK=GPIO12, WS=GPIO10, DO=GPIO9, DI=GPIO11 |
| PA enable | GPIO53 (NS4150B, high = amp on) |

---

## 5. Memory Layout

### PSRAM (32 MB MQPS64)

| Allocation | Size | Type |
|-----------|------|------|
| IQ ring (4 × 32 KiB slots) | 128 KiB | uint8 (CU8) |
| FFT scratch (2 × 1024 × 2 × 4 B) | 16 KiB | float32 complex |
| Spectrum double-buffer (2 × 512 × 2 B) | 2 KiB | int16 |
| Peak-hold (512 × 2 B) | 1 KiB | int16 |
| Waterfall buffer (720 × 240 × 1 B) | 169 KiB | uint8 (palette idx) |
| Waterfall LUT (256 × 4 B) | 1 KiB | uint32 |
| PCM ring (256 KiB) | 256 KiB | int16 stereo |
| LVGL draw buffer (2 × 720 × 720 × 2 B, RGB565) | ~2 MB | uint16 |
| LVGL object heap | ~512 KiB | — |
| **Total** | **≈ 3.5 MB** | of 32 MB available |

All large buffers allocated via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
Hot DSP scratch (FFT twiddle factors, Hann window table) in SRAM (`MALLOC_CAP_INTERNAL`).

### SRAM (~512 KB)

| Allocation | Size |
|-----------|------|
| Task stacks (Core 0: 16 K + 12 K + 8 K) | ~40 KB |
| Task stacks (Core 1: 16 K + 8 K + 8 K) | ~40 KB |
| Hann window table (1024 × int16) | 2 KB |
| FIR filter coefficients | ~4 KB |
| LVGL indev buffer | 2 KB |
| **Total** | **~90 KB** |

---

## 6. GPIO / Peripheral Map (Smart 86 Box)

Source: Waveshare `ESP32-P4-WIFI6-Touch-LCD-4B` documentation.

| Peripheral | Pins | Notes |
|-----------|------|-------|
| I2C shared bus (GT911, ES8311, ES7210, camera) | SDA=GPIO7, SCL=GPIO8 | 100 kHz I2C master |
| I2S TX (ES8311 DAC + NS4150B PA) | MCK=GPIO13, BCK=GPIO12, WS=GPIO10, DO=GPIO9, DI=GPIO11 | 48 kHz stereo 16-bit |
| Audio PA enable (NS4150B) | GPIO53 | High = amp on |
| Touch reset (GT911) | GPIO23 | Driven low at boot, then high |
| Touch INT | TP2 test point only (not a GPIO) | **Poll mode required** |
| LCD backlight EN | GPIO33 | |
| LCD backlight PWM | GPIO26 | LEDC, 0–95% |
| LCD reset | GPIO27 | |
| TF card (SDIO 4-bit) | D0–D3=GPIO39–42, CLK=GPIO43, CMD=GPIO44, PWR=GPIO45 | Deferred |
| USB HS OTG (RTL-SDR dongle) | USBD_P / USBD_N (fixed P4 pins) | Powered USB hub recommended |
| UART serial (CH343P) | GPIO37/RX, GPIO38/TX | For flashing/debugging only |
| ESP32-C6 (SDIO Wi-Fi) | GPIO6/14/16/18–20/22/54 | Deferred |
| Ethernet (IP101 RMII) | GPIO28–31, 34/35, 49–52 | Not used in V1 |
| Relays / RS485 (bottom board) | GPIO32/46 (relays), 47/48 (RS485) | Not used in V1 |

---

## 7. LVGL UI Layout (720 × 720)

Display: 4-inch 720 × 720 IPS (ST7703, MIPI DSI 2-lane).

```
┌──────────────────────────────────────────────────────────┐
│  VFO BAR (48 px)                                         │
│  [107.500 MHz]   [FM]  [Scope]  [Waterfall]  [Settings]  │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  SPECTRUM CHART (720 × 200 px)                           │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Peak-hold trace (white) + Live trace (green)      │  │
│  │  Center freq marker (red vertical line)            │  │
│  │  0 dBm ─────────────────────────────────────────── │  │
│  │  -50 ─────────────────────────────────────────────  │  │
│  │  -100─────────────────────────────────────────────  │  │
│  │  87.5 MHz       108 MHz           120 MHz          │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  WATERFALL (720 × 240 px)                                 │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Inferno colormap, 240 rows deep, scrolls down     │  │
│  │  New column enters at top, old rows scroll off     │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  CONTROL PANEL (720 × 132 px)                            │
│  ┌───────────┐ ┌────────────┐ ┌────────────────────┐    │
│  │ Freq tune │ │  Gain: 40  │ │  Signal: -62 dBm   │    │
│  │ [-][+]    │ │  [-][+]    │ │  S/N: 22 dB        │    │
│  │ [PRESET 1]│ │ [AGC Auto] │ │  [SCAN] [MARK]     │    │
│  │ [PRESET 2]│ │            │ │  Volume: [====|  ] │    │
│  └───────────┘ └────────────�� └────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

### LVGL Widget Mapping

| UI Element | LVGL Widget | Data Source | Update Rate |
|-----------|------------|-------------|-------------|
| VFO frequency | `lv_label` (32 pt monospace) | `esp_rtl_sdr` LO | On retune only |
| Band tabs | `lv_tabview` | UI navigation | On user tap |
| Spectrum | `lv_canvas` (PSRAM RGB565, 720×200) | `spectrum[512]` | 30 Hz |
| Waterfall | `lv_image` (PSRAM, 720×240) | Waterfall ring | ~4 columns/s |
| Gain slider | `lv_slider` (0–49 dB) | `esp_rtl_sdr_set_gain` | On user drag |
| AGC toggle | `lv_switch` | `set_tuner_gain_mode` | On toggle |
| Volume slider | `lv_slider` (0–100) | `es8311_voice_volume_set` | On user drag |
| Preset buttons | `lv_button` × 8 | NVS frequency slots | On tap |
| S/N meter | `lv_label` (24 pt) | `sdr_dsp` compute | 1 Hz |

### Touch Input

GT911 is **polled** (no INT pin on this board). Poll at 60 Hz in `lvgl_indev` task.
Coordinates: 0–719 × 0–719 (no rotation; `sw_rotate = false`).

---

## 8. Boot Sequence

```
app_main:
  1.  nvs_flash_init()
  2.  ESP_LOGI: "ESP32P4-RTL-SDR booting..."
  3.  bsp_display_start_with_config(&cfg)   → MIPI DSI + LVGL init
  4.  bsp_display_backlight_on()
  5.  Show splash screen (embedded bitmap "ESP32P4-RTL-SDR")
  6.  bsp_audio_codec_speaker_init()        → ES8311 I2C + I2S + PA GPIO53
  7.  sdr_control_init()                    → esp_rtl_sdr_install()
  8.  esp_rtl_sdr_get_capabilities() → log result
  9.  esp_rtl_sdr_start_hz(96100000, 960000) → wait for EVT_START
 10.  Create sdr_dsp task  (Core 0)
 11.  Create sdr_fm task   (Core 0)
 12.  Create audio_out task (Core 1)
 13.  UI: build dashboards, set initial freq = 96.1 MHz
 14.  Hide splash, show main dashboard
 15.  LVGL event loop running
```

---

## 9. Error Recovery

| Fault | Detection | Recovery |
|-------|-----------|----------|
| USB dongle unplugged mid-stream | `EVT_ERROR` + `metrics.last_error = ERR_USB` | Log; `stop()` → wait 1 s → `start_hz()` (hotplug retry) |
| FAULT state | `esp_rtl_sdr_get_state() == FAULT` | `reset()` → `start_hz()` |
| DSP overrun (ring full) | `metrics.consumer_drops > threshold` | Log warning; skip one FFT frame to catch up |
| I2S audio underrun | `i2s_channel_write` timeout | Silent-fill (zeros) to avoid clicks; log |
| LVGL frame drop | `lv_task_handler` delta > 20 ms | Reduce waterfall refresh to 10 Hz temporarily |
| PSRAM alloc failure | `heap_caps_malloc` returns NULL | `ESP_LOGE` + `abort()` (enable coredump) |

---

## 10. Component Dependency Graph

```
main  (app_main, sdr_control, DSP, audio, UI)
  │
  ├── esp_rtl_sdr  (hardcoreerik/esp-rtl-sdr v0.9.3, USB Host + RTL2832u)
  │     └── usb  (ESP-IDF USB Host, ESP32-P4 HS)
  │
  ├── waveshare BSP  (waveshare/esp32_p4_wifi6_touch_lcd_4b v3.0.1)
  │     ├── esp_lcd_st7703         (MIPI DSI panel driver)
  │     ├── esp_lvgl_adapter       (LVGL port)
  │     ├── esp_lcd_touch_gt911    (GT911 I2C touch)
  │     ├── esp_codec_dev          (ES8311 + ES7210 audio)
  │     └── esp-bsp                (board init, backlight, audio)
  │
  ├── lvgl__lvgl  (LVGL v9.2.0, UI framework)
  │     └── esp_lvgl_port
  │
  ├── esp-dsp  (espressif/esp-dsp: FFT, FIR, IIR, window functions)
  │
  ├── esp_driver_i2s   (I2S TX to ES8311)
  ├── esp_driver_i2c   (shared bus: GT911 + ES8311 + ES7210)
  ├── esp_driver_gpio  (PA enable, LCD reset)
  ├── nvs_flash        (settings persistence)
  └── freertos         (OS primitives)
```

---

## 11. Deferred / Out of Scope (V1)

| Feature | Target Phase |
|---------|-------------|
| Wi-Fi (ESP32-P4 native or C6 SDIO) | Phase 6+ |
| ADS-B 1090 MHz | Phase 5 |
| AM / NFM demod | Phase 5 |
| Stereo FM demod | Phase 5 |
| RDS decode | Phase 5 |
| TF card recording (IQ / WAV) | Phase 6 |
| ACARS / P25 / POCSAG | Future |
| 3D spectrum / polar plot | Future |
| Multi-dongle | Future |
