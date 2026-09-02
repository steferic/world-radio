#include "audio_pipe.h"

#include <string.h>
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "audio_pipe";
static RingbufHandle_t s_ringbuf = NULL;
static size_t s_capacity_bytes = 0;

atomic_uint_fast32_t g_audio_pipe_write_drops = 0;
atomic_uint_fast32_t g_audio_pipe_read_underruns = 0;
atomic_uint_fast32_t g_audio_pipe_resyncs = 0;
atomic_uint_fast32_t g_audio_pipe_rate_changes = 0;

esp_err_t audio_pipe_init(size_t capacity_bytes)
{
    // xRingBufferCreateWithCaps, as opposed to xRingBufferCreate, puts the audio buffer in
    // the external PSRAM memory that is included alongside regular internal SRAM.
    s_ringbuf = xRingbufferCreateWithCaps(capacity_bytes, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ringbuf == NULL) {
        ESP_LOGE(TAG, "failed to allocate %u byte ring buffer", (unsigned)capacity_bytes);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ring buffer at %p (PSRAM if 0x3C..., internal if 0x3FC...)", (void *)s_ringbuf);
    s_capacity_bytes = capacity_bytes;
    return ESP_OK;
}

bool audio_pipe_write(const uint8_t *data, size_t len, TickType_t timeout)
{
    if (s_ringbuf == NULL || len == 0) {
        return false;
    }
    return xRingbufferSend(s_ringbuf, data, len, timeout) == pdTRUE;
}

size_t audio_pipe_read(uint8_t *out, size_t max_len, TickType_t timeout)
{
    if (s_ringbuf == NULL || max_len == 0) {
        return 0;
    }

    size_t item_size = 0;
    // For a byte-type ring buffer this returns a pointer straight into the
    // buffer (no framing), capped at max_len bytes and at whatever is
    // contiguous before the buffer wraps around.
    void *item = xRingbufferReceiveUpTo(s_ringbuf, &item_size, timeout, max_len);
    if (item == NULL) {
        return 0;
    }

    memcpy(out, item, item_size);
    vRingbufferReturnItem(s_ringbuf, item);
    return item_size;
}

void audio_pipe_reset(void)
{
    if (s_ringbuf == NULL) {
        return;
    }
    size_t item_size = 0;
    void *item;
    while ((item = xRingbufferReceiveUpTo(s_ringbuf, &item_size, 0, 4096)) != NULL) {
        vRingbufferReturnItem(s_ringbuf, item);
    }
}

size_t audio_pipe_get_fill_bytes(void)
{
    if (s_ringbuf == NULL) {
        return 0;
    }
    // Byte-type ring buffers report free space directly; fill = capacity - free.
    size_t free_bytes = xRingbufferGetCurFreeSize(s_ringbuf);
    return s_capacity_bytes - free_bytes;
}

size_t audio_pipe_get_capacity_bytes(void)
{
    return s_capacity_bytes;
}
