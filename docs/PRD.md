# PRD — ESP32P4-RTL-SDR

> Project name: ESP32P4-RTL-SDR
> Target hardware: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Smart 86 Box)
> SDR dongle: RTL-SDR Blog V3 (R820T2, from RTL-SDR.com)
> Driver: hardcoreerik/esp-rtl-sdr v0.9.3
> Reference application: hardcoreerik/OrcSDR (M5Stack Tab5 target)
> Status: Planning / Pre-development

---

## 1. Product Summary

ESP32P4-RTL-SDR is a firmware application that turns the Waveshare Smart 86 Box
(ESP32-P4, 720×720 touch display) + RTL-SDR Blog V3 into a portable,
touch-operated SDR without any external PC. The user plugs the dongle into
the board's USB OTG port and immediately has a real-time spectrum analyzer,
scrolling waterfall, and FM radio with speaker output.

**Goal:** Achieve parity with the core features of OrcSDR's Home dashboard
(spectrum + waterfall + FM listen + VFO) on the Waveshare P4 hardware.

---

## 2. Feature List by Priority

### P0 — MVP (Phase 1–3, first usable build)

| ID | Feature | Acceptance Criteria |
|----|---------|--------------------|
| P0-1 | **RTL-SDR detection & streaming** | Serial log confirms `EVT_START`, `effective_sps > 900K`, no overruns in 30 s |
| P0-2 | **Boot & splash screen** | Display shows splash → main dashboard within 5 s of power-on |
| P0-3 | **Real-time spectrum (512 bins)** | Spectrum updates at ≥ 30 Hz; peak-hold trace visible; live trace green; 512 bins across current span |
| P0-4 | **Scrolling waterfall (720 px wide)** | Waterfall scrolls at ~4 columns/s; inferno colormap; 240 rows deep |
| P0-5 | **VFO display & tune** | Frequency display in MHz (4 decimal places); +/- step buttons (100 K / 10 K / 1 K); tap-to-tune on spectrum; `retune_hz` applied within 50 ms |
| P0-6 | **Gain control (manual 0–49 dB)** | Slider adjusts gain; UI label shows current value; `esp_rtl_sdr_set_gain` confirmed via log |
| P0-7 | **FM audio output (mono, 48 kHz)** | Tuned FM station audible through 8 Ω speaker; no clipping at 50% volume; no audible artifacts at 107.5 MHz |
| P0-8 | **Volume control (0–100)** | Slider adjusts speaker volume; zero = muted |
| P0-9 | **Touch input working** | All buttons/sliders respond to GT911 touch within 100 ms |
| P0-10 | **Persistence (NVS)** | Gain, volume, and last frequency survive reboot |

### P1 — Phase 4 (FM Radio Dashboard)

| ID | Feature | Acceptance Criteria |
|----|---------|--------------------|
| P1-1 | **FM band presets (8 slots)** | 8 preset buttons store/recall frequency + gain; NVS-backed |
| P1-2 | **FM band scan** | Sweep 87.5–108 MHz; stop at peak > -50 dBm; show station frequency |
| P1-3 | **Signal level meter** | Average of 100 bins around carrier → dBm label; S/N estimate at 1 Hz |
| P1-4 | **FM stereo demod** | L/R separation via BPF ±23 kHz + Hilbert; audible stereo on dual-channel |
| P1-5 | **PPM crystal correction (±200)** | PPM slider in settings; `esp_rtl_sdr_set_ppm` confirmed |
| P1-6 | **AGC auto toggle** | Switch in settings; `set_tuner_gain_mode(AUTO/MANUAL)` |

### P2 — Phase 5+ (Stretch / Deferred)

| ID | Feature | Notes |
|----|---------|-------|
| P2-1 | AM / NFM demod | Envelope detector (AM); 3 kHz IIR + quadrature (NFM) |
| P2-2 | RDS decode | 50 Hz subcarrier → 2-FSK 2700 baud → CRC16 → PI/PS/PT/PTY display |
| P2-3 | ADS-B 1090 MHz | Mode-S decoder; dedicated dashboard; 2.048 MSPS required |
| P2-4 | TF card recording | SDIO 4-bit → IQ/WAV capture to TF card |
| P2-5 | Wi-Fi / C6 coprocessor | NTP time sync; optional network features |
| P2-6 | ACARS / P25 / POCSAG | Digital mode decoders |

---

## 3. UI Wireframes

### 3.1 Main Dashboard — Scope Mode (720 × 720)

```
┌─────────────────────────────────────��────────────────────────┐
│  107.500 MHz  [FM] [Scope] [Waterfall] [Settings]   ← VFO BAR 48 px
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  SPECTRUM  (720 × 200 px, lv_canvas, RGB565 PSRAM)    │  │
│  │                                                        │  │
│  │  Peak-hold (white)  Live (green)  Center marker (red)  │  │
│  │  0 dBm ─────────────────────────────────────────────   │  │
│  │  -50 ─────────────────────────────────────────────     │  │
│  │  -100─────────────────────────────────────────────     │  │
│  │  87.5 MHz        108 MHz          120 MHz             │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  WATERFALL  (720 × 240 px, lv_image, palette-indexed) │  │
│  │  Inferno LUT, column-major, scrolls top→bottom        │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌────────────┐ ┌─────────────┐ ┌────────────────────────┐  │
│  │ Freq tune  │ │ Gain: 40 dB │ │ Signal: -62 dBm        │  │
│  │ [-][+]     │ │ [-][+]      │ │ S/N: 22 dB             │  │
│  │ [PRESET 1] │ │ [AGC: AUTO] │ │ [SCAN] [MARK]          │  │
│  │ [PRESET 2] │ │             │ │ Volume: [====|  ] 60   │  │
│  └────────────┘ └─────────────┘ └────────────────────────┘  │
│                    ← CONTROL PANEL 132 px
└──────────────────────────────────────────────────────────────┘
```

### 3.2 FM Radio Dashboard (Phase 4)

```
┌──────────────────────────────────────────────────────────────┐
│  107.500 MHz  [FM] [Scope] [Waterfall] [Settings]  ← VFO BAR
├──────────────────────────────────────────────────────────────┤
│  Station:  (RDS text, Phase 4)                              │
│  PI: 0x0000   PTY: Pop   PS: "FM 107.5"                     │
│                                                              │
│  [P1] [P2] [P3] [P4] [P5] [P6] [P7] [P8]   ← Preset row   │
│                                                              │
│  [SEEK -]  [SEEK +]   [SCAN]   [MARK]                       │
│                                                              │
│  L: [====|  ]  R: [=====| ]   ← Stereo meters (Phase 4)    │
│  Stereo: LOCKED  Mono: —                                     │
│                                                              │
│  Volume: [=======|  ]  70                                   │
│  Gain: 40 dB   PPM: +0                                       │
└──────────────────────────────────────────────────────────────┘
```

### 3.3 Settings Dashboard

```
┌──────────────────────────────────────────────────────────────┐
│  Settings                                                   │
├──────────────────────────────────────────────────────────────┤
│  Display                                                    │
│  Backlight: [=====|]  80%                                  │
│  PPM Correction: [----|]  0 ppm                             │
│                                                              │
│  Receiver                                                   │
│  Gain Mode: [AGC AUTO] / [Manual]                            │
│  Sample Rate: [960K] [1.92M] [2.048M]                       │
│  Center Freq: 107.500 MHz   Span: 5 MHz                    │
│                                                              │
│  Audio                                                        │
│  Output Volume: [====|]  60                                  │
│                                                              │
│  About                                                        │
│  Firmware: v0.1.0   Driver: esp_rtl_sdr v0.9.3              │
│  Hardware: Smart 86 + RTL-SDR V3 R820T2                     │
└──────────────────────────────────────────────────────────────┘
```

---

## 4. Performance Requirements

| Metric | Requirement |
|--------|------------|
| Spectrum update rate | ≥ 30 Hz (512 bins) |
| Waterfall scroll rate | ≥ 3 columns/s (720 px wide, 240 rows) |
| Audio latency (demod → speaker) | < 50 ms (48 kHz, I2S DMA 2 ms blocks) |
| Boot time (power-on to first spectrum) | < 5 s |
| UI responsiveness (touch → screen) | < 100 ms |
| CPU headroom (Core 0 DSP) | ≥ 50% at 960 KSPS steady-state |
| PSRAM usage (steady-state) | < 8 MB (of 32 MB) |
| Flash usage (firmware + assets) | < 8 MB (of 32 MB) |

---

## 5. Supported Frequency Range

| Band | Range | Notes |
|------|-------|-------|
| R820T2 normal tuner | 24 MHz – 1.766 GHz | Standard operation; gain 0–49 dB |
| Q-branch direct sampling | 24 kHz – 24 MHz | `CAP_DIRECT_SAMPLING`; gain bypassed (digital IF only); sensitivity degrades |
| FM broadcast | 87.5 – 108 MHz | Primary use case; P25 kHz channel spacing |
| NOAA WX | 162.400 – 162.550 MHz | Preset |
| Airband | 118.000 – 136.975 MHz | 25 / 8.33 kHz steps (Phase 4+) |
| VHF marine | 156.000 – 174.000 MHz | Channel presets (Phase 4+) |
| 2 m amateur | 144 – 148 MHz | Phase 4+ |
| ADS-B | 1090 MHz | Phase 5 (2.048 MSPS required) |

---

## 6. Out of Scope (V1)

| Item | Reason |
|------|--------|
| Wi-Fi / C6 coprocessor | Deferred to Phase 6+; P4's own Wi-Fi 6 not enabled in V1 sdkconfig |
| ADS-B / Mode-S | Requires 2.048 MSPS; deferred to Phase 5 |
| TF card recording | Deferred to Phase 6 |
| Stereo FM | Deferred to Phase 5 |
| RDS | Deferred to Phase 5 |
| ACARS / P25 / POCSAG | Future |
| 3D / polar spectrum | Future |
| Multi-dongle | Future |
| Ethernet | Not used in V1 |
| RS485 / Relays | Not used in V1 |

---

## 7. Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|-----------|------------|
| `esp-rtl-sdr` v0.9.3 not yet tested on ESP-IDF 6.1.0 (target was 5.5.4) | Build failure | Medium | Test build in Phase 1 task 1.1; if API changed, pin to IDF 5.5.x or patch the component |
| `waveshare/esp32_p4_wifi6_touch_lcd_4b` v3.0.1 API changes in newer BSP releases | Display/audio init break | Low | Pin BSP version in `idf_component.yml`; test in Phase 1 |
| RTL-SDR V3 not fully supported by esp-rtl-sdr (V4 is primary target) | V3 stream fails or degraded | Medium | Verify `get_capabilities()` in Phase 1; use V3-specific profile if available; log exact errors |
| USB power insufficient for RTL-SDR V3 LNA | Dongle not detected or resets | Medium | Use powered USB hub or 5 V supply; check Smart 86 USB 5 V rail |
| FM stereo demod too heavy for single core at 960 KSPS | Audio stutter | Medium | Use decimated path (480 KSPS); mono for V1; stereo as separate task with lower priority |
| PSRAM bandwidth contention (LVGL + DSP + audio) | Frame drops / audio underrun | Low | Use software LVGL renderer; keep waterfall as palette-indexed (1 B/px); PCM ring in PSRAM |
| GT911 poll latency on P4 I2C | UI feel sluggish | Low | 16 ms poll (60 Hz) acceptable; no INT pin on this board — confirmed by BSP docs |
| 720×720 display orientation (roundable square) | Layout planning | Low | Use `sw_rotate = false`; plan UI for square display |

---

## 8. Milestones

| Milestone | Deliverable | Est. Days from start |
|-----------|-------------|---------------------|
| **M1** | Display boots + RTL-SDR detected + IQ streaming logged in serial | 5 |
| **M2** | Real-time spectrum (512 bins) + waterfall visible on screen | 10 |
| **M3** | FM demod + audio out speaker (mono) + VFO UI + gain/volume | 15 |
| **M4** | FM dashboard: 8 presets, band scan, RDS, stereo | 22 |
| **M5** | Polish: AM mode, NVS persistence, error handling, settings screen | 28 |
