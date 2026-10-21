/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * iq_ring — lock-free single-producer single-consumer (SPSC) ring of fixed-size
 * IQ slots.
 *
 * Producer: the esp_rtl_sdr USB owner task, inside the EVT_IQ_BLOCK callback
 *           (must return quickly; the copy is the only work done here).
 * Consumer: the sdr_dsp task (Core 0, priority 12), one at a time.
 *
 * There is no mutex: correctness relies on a single writer of head/tail plus
 * acquire/release memory ordering (RISC-V ld.aq / st.rl via esp_rom or the
 * port-atomic primitives). The slot buffer itself lives in PSRAM; the two
 * index words live in the struct.
 *
 * "Full" is detected by comparing head vs tail modulo the slot count; with a
 * power-of-two slot count the modulo is a mask.
 */

/** Default slot size: matches the driver's default bulk URB (16 KiB, CU8). */
#define IQ_RING_DEFAULT_SLOT_BYTES  16384u
/** Default slot count: 8 slots x 16 KiB = 128 KiB PSRAM (per ARCHITECTURE). */
#define IQ_RING_DEFAULT_SLOT_COUNT  8u

/**
 * A single fixed-size slot view returned by iq_ring_pop(). Valid only until
 * the next pop call on the same ring. The consumer must finish reading before
 * calling iq_ring_pop() again (the slot may be reused by the producer).
 */
typedef struct {
    const uint8_t *data;   /**< Pointer into the ring, slot-aligned. */
    uint32_t        bytes; /**< Bytes valid in this slot (<= slot_bytes). */
    uint32_t        seq;  /**< Monotonic sequence number (wraps with uint32). */
} iq_ring_view_t;

/**
 * Opaque ring handle. Allocate with iq_ring_alloc(). The struct is NOT
 * exposed; all access goes through the functions below.
 */
typedef struct iq_ring iq_ring_t;

/**
 * Allocate an SPSC ring of `slot_count` slots, each `slot_bytes` bytes, in
 * PSRAM (SPIRAM-capable).
 *
 * @param slot_bytes   Must be > 0 and a multiple of 2 (CU8 IQ pairs). Any
 *                     power-of-two slot_count is required for mask-based
 *                     indexing; non-power-of-two counts fall back to a
 *                     slow path. Default 16384.
 * @param slot_count   Number of slots; must be a power of two, >= 2.
 *                     Default 8.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on bad params,
 *         ESP_ERR_NO_MEM if PSRAM cannot satisfy the request.
 */
esp_err_t iq_ring_alloc(uint32_t slot_bytes, uint32_t slot_count);

/**
 * Free the ring and all associated PSRAM. Safe to call with a NULL/unused
 * handle. Idempotent at the allocator level.
 */
void iq_ring_free(void);

/**
 * Register a ring at a given priority slot. One global ring is the intended
 * use in V1; the API is single-handle for clarity.
 */
void iq_ring_use(void);

/**
 * True once iq_ring_alloc() has succeeded.
 */
bool iq_ring_is_ready(void);

/**
 * Producer path (EVT_IQ_BLOCK). Copy up to one slot's worth of the IQ block
 * into the next free slot.
 *
 * If the ring is full, the OLDEST slot is overwritten (a drop) and the
 * overflow counter is incremented. This keeps the producer non-blocking and
 * always returns fast, as required by the driver callback.
 *
 * @param src     Pointer to the CU8 IQ bytes from the driver block.
 * @param len     Number of bytes available (block->bytes).
 * @return ESP_OK on success (copied), ESP_ERR_INVALID_ARG if src is NULL
 *         or len == 0, ESP_ERR_INVALID_STATE if the ring is not allocated.
 */
esp_err_t iq_ring_push(const uint8_t *src, uint32_t len);

/**
 * Consumer path. Pop the next filled slot.
 *
 * @param out     Where to write the slot view. Out-of-range / empty slots
 *                set out->bytes to 0.
 * @return ESP_OK on success (a view was written; check out->bytes to see if
 *         it was a real block or a no-op when the ring was empty),
 *         ESP_ERR_INVALID_STATE if the ring is not allocated,
 *         ESP_ERR_INVALID_ARG if out is NULL.
 */
esp_err_t iq_ring_pop(iq_ring_view_t *out);

/**
 * Number of slots currently holding data (0 .. slot_count). Non-atomic
 * snapshot; valid for logging / metrics, not for decision-making.
 */
uint32_t iq_ring_count(void);

/**
 * Peak fill (slot count) observed since allocation. For pressure metrics.
 */
uint32_t iq_ring_high_water(void);

/**
 * Cumulative bytes dropped by overwrite-on-full (producer side). Unit: bytes.
 */
uint64_t iq_ring_overrun_bytes(void);

/**
 * Cumulative number of times the producer overwrote a slot due to full.
 */
uint32_t iq_ring_overrun_events(void);

/**
 * Reset all counters to 0 and empty the ring. Does not deallocate.
 */
void iq_ring_reset(void);

#ifdef __cplusplus
}
#endif
