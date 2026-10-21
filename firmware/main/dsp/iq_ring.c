/*
 * SPDX-FileCopyrightText: 2026 ESP32P4-RTL-SDR
 *
 * SPDX-License-Identifier: MIT
 */

#include "iq_ring.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "dsp_ring";

/*
 * RISC-V (and all supported ports) acquire/release ordering for the two
 * index words. ESP-IDF provides these through the __atomic builtins, which
 * map to ld.aq / st.rl on RISC-V. We use the builtins directly (C11) so the
 * code is explicit and not hidden behind a macro.
 */
static inline uint32_t ir_load_acquire(const volatile uint32_t *p)
{
    return __atomic_load_n((const uint32_t *)p, __ATOMIC_ACQUIRE);
}

static inline void ir_store_release(volatile uint32_t *p, uint32_t v)
{
    __atomic_store_n((uint32_t *)p, v, __ATOMIC_RELEASE);
}

struct iq_ring {
    uint8_t *psram;              /* Slot storage, SPIRAM-capable. */
    uint32_t slot_bytes;
    uint32_t slot_count;         /* Must be a power of two, >= 2. */
    uint32_t mask;               /* slot_count - 1 */
    volatile uint32_t head;      /* Written by producer, read by consumer. */
    volatile uint32_t tail;      /* Written by consumer, read by producer. */
    uint32_t seq;                /* Next sequence number to stamp. */
    uint32_t high_water;         /* Peak fill observed. */
    uint64_t overrun_bytes;
    uint32_t overrun_events;
    bool     ready;
};

static struct iq_ring s_ring;

esp_err_t iq_ring_alloc(uint32_t slot_bytes, uint32_t slot_count)
{
    if (slot_bytes == 0 || slot_bytes % 2u != 0u) {
        ESP_LOGE(TAG, "slot_bytes must be > 0 and even");
        return ESP_ERR_INVALID_ARG;
    }
    if (slot_count == 0 || (slot_count & (slot_count - 1u)) != 0u || slot_count < 2u) {
        ESP_LOGE(TAG, "slot_count must be a power of two, >= 2");
        return ESP_ERR_INVALID_ARG;
    }

    size_t total = (size_t)slot_bytes * slot_count;
    uint8_t *buf = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "PSRAM alloc failed: %u bytes", (unsigned)total);
        return ESP_ERR_NO_MEM;
    }

    memset(&s_ring, 0, sizeof(s_ring));
    s_ring.psram       = buf;
    s_ring.slot_bytes  = slot_bytes;
    s_ring.slot_count  = slot_count;
    s_ring.mask        = slot_count - 1u;
    s_ring.seq         = 0u;
    s_ring.high_water  = 0u;
    s_ring.overrun_bytes  = 0ull;
    s_ring.overrun_events = 0u;
    s_ring.head      = 0u;
    s_ring.tail      = 0u;
    s_ring.ready     = true;

    ESP_LOGI(TAG, "IQ ring allocated: %u slots x %u bytes = %u KiB PSRAM",
             (unsigned)slot_count, (unsigned)slot_bytes,
             (unsigned)(total / 1024u));
    return ESP_OK;
}

void iq_ring_free(void)
{
    if (!s_ring.ready) {
        return;
    }
    heap_caps_free(s_ring.psram);
    s_ring.psram = NULL;
    s_ring.ready = false;
}

void iq_ring_use(void)
{
    /* V1 single-instance ring: the allocator sets the global. */
}

bool iq_ring_is_ready(void)
{
    return s_ring.ready;
}

esp_err_t iq_ring_push(const uint8_t *src, uint32_t len)
{
    if (!s_ring.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (src == NULL || len == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t head = ir_load_acquire(&s_ring.head);
    uint32_t slot_idx = head & s_ring.mask;
    uint32_t this_bytes = (len < s_ring.slot_bytes) ? len : s_ring.slot_bytes;

    /* Overwrite-on-full detection: the consumer's tail is one ahead of the
     * slot we are about to write iff the ring is exactly full. */
    uint32_t tail = ir_load_acquire(&s_ring.tail);
    uint32_t fill = head - tail;
    if (fill >= s_ring.slot_count) {
        s_ring.overrun_events++;
        s_ring.overrun_bytes += this_bytes;
    }

    uint8_t *dst = s_ring.psram + (size_t)slot_idx * s_ring.slot_bytes;
    memcpy(dst, src, this_bytes);

    /* Advance head with release semantics so the consumer sees the data. */
    ir_store_release(&s_ring.head, head + 1u);

    if (fill > s_ring.high_water) {
        s_ring.high_water = fill;
    }

    /*
     * We do not stamp the per-slot seq / seq counter inside the slot here
     * because that would require the consumer to read a slot-local header.
     * The consumer obtains a monotonic seq from its own tail advancement
     * (see iq_ring_pop()), which is equivalent for SPSC and cheaper.
     */
    (void)s_ring.seq;

    return ESP_OK;
}

esp_err_t iq_ring_pop(iq_ring_view_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ring.ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t tail = ir_load_acquire(&s_ring.tail);
    uint32_t head = ir_load_acquire(&s_ring.head);

    if (head == tail) {
        out->data = NULL;
        out->bytes = 0u;
        out->seq = 0u;
        return ESP_OK;
    }

    uint32_t slot_idx = tail & s_ring.mask;
    out->data = s_ring.psram + (size_t)slot_idx * s_ring.slot_bytes;
    out->bytes = s_ring.slot_bytes;
    out->seq = tail;  /* Monotonic per-pop sequence; wraps with uint32. */

    ir_store_release(&s_ring.tail, tail + 1u);
    return ESP_OK;
}

uint32_t iq_ring_count(void)
{
    if (!s_ring.ready) {
        return 0u;
    }
    uint32_t head = ir_load_acquire(&s_ring.head);
    uint32_t tail = ir_load_acquire(&s_ring.tail);
    return head - tail;
}

uint32_t iq_ring_high_water(void)
{
    return s_ring.high_water;
}

uint64_t iq_ring_overrun_bytes(void)
{
    return s_ring.overrun_bytes;
}

uint32_t iq_ring_overrun_events(void)
{
    return s_ring.overrun_events;
}

void iq_ring_reset(void)
{
    if (!s_ring.ready) {
        return;
    }
    /* Safe only when no producer/consumer is in flight (e.g. between streams). */
    s_ring.head = 0u;
    s_ring.tail = 0u;
    s_ring.seq = 0u;
    s_ring.high_water = 0u;
    s_ring.overrun_bytes = 0ull;
    s_ring.overrun_events = 0u;
}
