#include "mem_probe.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_debug_helpers.h"
#include "mbedtls/platform.h"

static const char *TAG = "mem_probe";

static volatile size_t s_tls_bytes = 0;
static volatile size_t s_tls_peak  = 0;

static void *tls_calloc(size_t n, size_t sz)
{
    size_t bytes = n * sz;
    void *p = heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGE(TAG, "TLS ALLOC FAIL %u B | int_free=%u int_largest=%u tls_outstanding=%u tls_peak=%u",
                 (unsigned)bytes,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)s_tls_bytes,
                 (unsigned)s_tls_peak);
        return NULL;
    }
    s_tls_bytes += bytes;
    if (s_tls_bytes > s_tls_peak) s_tls_peak = s_tls_bytes;
    if (bytes >= 1024) {
        ESP_LOGI(TAG, "TLS +%u B | outstanding=%u peak=%u int_free=%u int_largest=%u",
                 (unsigned)bytes,
                 (unsigned)s_tls_bytes,
                 (unsigned)s_tls_peak,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
    return p;
}

static void tls_free(void *p)
{
    if (!p) return;
    size_t bytes = heap_caps_get_allocated_size(p);
    if (bytes <= s_tls_bytes) s_tls_bytes -= bytes;
    heap_caps_free(p);
}

static void IRAM_ATTR on_alloc_fail(size_t size, uint32_t caps, const char *fn)
{
    ESP_EARLY_LOGE(TAG,
        "ALLOC FAIL %u B caps=0x%08x in %s | int_free=%u int_largest=%u dma_free=%u dma_largest=%u tls_outstanding=%u",
        (unsigned)size, (unsigned)caps, fn ? fn : "?",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)s_tls_bytes);
    esp_backtrace_print(12);
}

void mem_probe_snapshot(const char *label)
{
    ESP_LOGI(TAG, "%s | int_free=%u int_largest=%u dma_free=%u dma_largest=%u tls_outstanding=%u tls_peak=%u",
             label ? label : "snapshot",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)s_tls_bytes,
             (unsigned)s_tls_peak);
}

size_t mem_probe_tls_bytes(void) { return s_tls_bytes; }
size_t mem_probe_tls_peak(void)  { return s_tls_peak;  }

esp_err_t mem_probe_init(void)
{
    mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
    esp_err_t err = heap_caps_register_failed_alloc_callback(on_alloc_fail);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_failed_alloc_callback: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "installed");
    return ESP_OK;
}