// Memory sanity report for ESP32-S3.
//
// On boot it prints:
//   1. Chip model / revision / cores / ESP-IDF version
//   2. External flash: chip-detected size and configured size
//   3. External PSRAM: whether it initialized, size, base address, mode
//   4. Every heap capability pool (INTERNAL, DMA, SPIRAM, EXEC, RTCRAM, ...)
//      with total / free / largest-contiguous / min-ever-free
//   5. The multi_heap per-region breakdown (physical bank granularity)
//   6. Probe allocations at 4 KB / 32 KB / 128 KB / 512 KB / 1 MB in each
//      pool, so you can see exactly which sizes fit where at boot
//   7. A cheat sheet for the S3 virtual address space
//
// Everything after boot is derived from runtime ESP-IDF APIs -- if a number
// here is "wrong" it is because the runtime disagrees with what you thought
// you configured, which is the useful thing to know.

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

static void fmt_bytes(size_t bytes, char *out, size_t outlen)
{
    if (bytes >= 1024u * 1024u) {
        snprintf(out, outlen, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, outlen, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(out, outlen, "%u B", (unsigned)bytes);
    }
}

static void print_cap_pool(const char *label, uint32_t caps)
{
    multi_heap_info_t info;
    memset(&info, 0, sizeof(info));
    heap_caps_get_info(&info, caps);

    size_t total = info.total_free_bytes + info.total_allocated_bytes;
    char t[16], f[16], l[16], m[16];
    fmt_bytes(total,                    t, sizeof(t));
    fmt_bytes(info.total_free_bytes,    f, sizeof(f));
    fmt_bytes(info.largest_free_block,  l, sizeof(l));
    fmt_bytes(info.minimum_free_bytes,  m, sizeof(m));

    printf("  %-22s  total=%-10s  free=%-10s  largest=%-10s  min_ever_free=%s\n",
           label, t, f, l, m);
}

static void probe_alloc(const char *pool_label, uint32_t caps, size_t sz)
{
    void *p = heap_caps_malloc(sz, caps);
    char s[16];
    fmt_bytes(sz, s, sizeof(s));
    if (p) {
        // Report the physical address the allocator handed back -- an
        // allocation "in SPIRAM" should be at 0x3C000000+, "in INTERNAL"
        // should be at 0x3FC80000+ on the S3.
        printf("    %-10s in %-10s -> OK   @ 0x%08" PRIxPTR "\n",
               s, pool_label, (uintptr_t)p);
        heap_caps_free(p);
    } else {
        printf("    %-10s in %-10s -> FAIL (no block that large in that pool)\n",
               s, pool_label);
    }
}

static const char *chip_model_name(esp_chip_model_t m)
{
    switch (m) {
        case CHIP_ESP32:    return "ESP32";
        case CHIP_ESP32S2:  return "ESP32-S2";
        case CHIP_ESP32S3:  return "ESP32-S3";
        case CHIP_ESP32C3:  return "ESP32-C3";
        case CHIP_ESP32C2:  return "ESP32-C2";
        case CHIP_ESP32C6:  return "ESP32-C6";
        case CHIP_ESP32H2:  return "ESP32-H2";
        default:            return "unknown";
    }
}

void app_main(void)
{
    // Small delay so the boot log finishes flushing before our report starts.
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("\n\n");
    printf("======================================================\n");
    printf("           ESP32 MEMORY SANITY REPORT\n");
    printf("======================================================\n\n");

    // ------------------------------------------------------------------
    // 1. Chip
    // ------------------------------------------------------------------
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("CHIP\n");
    printf("  target (sdkconfig) : %s\n", CONFIG_IDF_TARGET);
    printf("  model  (runtime)   : %s\n", chip_model_name(chip.model));
    printf("  cores              : %d\n", chip.cores);
    printf("  silicon rev        : v%d.%d\n",
           chip.revision / 100, chip.revision % 100);
    printf("  features           : %s%s%s%s%s\n",
           (chip.features & CHIP_FEATURE_WIFI_BGN)  ? "WiFi "     : "",
           (chip.features & CHIP_FEATURE_BT)        ? "BT "       : "",
           (chip.features & CHIP_FEATURE_BLE)       ? "BLE "      : "",
           (chip.features & CHIP_FEATURE_EMB_FLASH) ? "embFlash " : "",
           (chip.features & CHIP_FEATURE_EMB_PSRAM) ? "embPSRAM " : "");
    printf("  ESP-IDF            : %s\n", esp_get_idf_version());
    printf("\n");

    // ------------------------------------------------------------------
    // 2. Flash
    // ------------------------------------------------------------------
    printf("EXTERNAL FLASH (SPI NOR)\n");
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        char b[16];
        fmt_bytes(flash_size, b, sizeof(b));
        printf("  configured size    : %s\n", b);
    } else {
        printf("  configured size    : (esp_flash_get_size failed)\n");
    }
    printf("  chip-detected size : see boot log line 'spi_flash: detected chip'\n");
    printf("  configured mode    : "
#if   CONFIG_ESPTOOLPY_FLASHMODE_QIO
        "QIO"
#elif CONFIG_ESPTOOLPY_FLASHMODE_QOUT
        "QOUT"
#elif CONFIG_ESPTOOLPY_FLASHMODE_DIO
        "DIO"
#elif CONFIG_ESPTOOLPY_FLASHMODE_DOUT
        "DOUT"
#else
        "(unknown)"
#endif
        "\n");
    printf("\n");

    // ------------------------------------------------------------------
    // 3. External PSRAM
    // ------------------------------------------------------------------
    printf("EXTERNAL PSRAM (SPI SRAM)\n");
#if CONFIG_SPIRAM
    printf("  sdkconfig          : CONFIG_SPIRAM=y\n");
#if   CONFIG_SPIRAM_MODE_OCT
    printf("  configured mode    : OCTAL (S3 R8 / N16R8)\n");
#elif CONFIG_SPIRAM_MODE_QUAD
    printf("  configured mode    : QUAD  (S3 R2 / N8R2, plus R4/R8 in fallback)\n");
#else
    printf("  configured mode    : (unknown)\n");
#endif
    if (esp_psram_is_initialized()) {
        size_t s = esp_psram_get_size();
        char b[16];
        fmt_bytes(s, b, sizeof(b));
        printf("  runtime state      : INITIALIZED\n");
        printf("  detected size      : %s\n", b);
        printf("  base address       : 0x3C000000 (data view)\n");
    } else {
        printf("  runtime state      : NOT INITIALIZED\n");
        printf("  likely cause       : chip has no PSRAM, or the configured mode\n");
        printf("                       (QUAD vs OCTAL) does not match this board\n");
    }
#else
    printf("  sdkconfig          : CONFIG_SPIRAM is DISABLED\n");
    printf("  runtime state      : n/a\n");
#endif
    printf("\n");

    // ------------------------------------------------------------------
    // 4. Heap pools by capability
    // ------------------------------------------------------------------
    printf("HEAP POOLS (by capability mask)\n");
    printf("  These are the pools multi_heap can serve. Same physical bytes\n");
    printf("  can appear under multiple caps (e.g. INTERNAL is a subset of\n");
    printf("  DEFAULT; DMA is a subset of INTERNAL).\n\n");
    print_cap_pool("INTERNAL",         MALLOC_CAP_INTERNAL);
    print_cap_pool("INTERNAL | DMA",   MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    print_cap_pool("INTERNAL | 8BIT",  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    print_cap_pool("INTERNAL | 32BIT", MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    print_cap_pool("DMA",              MALLOC_CAP_DMA);
    print_cap_pool("SPIRAM",           MALLOC_CAP_SPIRAM);
    print_cap_pool("SPIRAM | 8BIT",    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    print_cap_pool("EXEC (IRAM)",      MALLOC_CAP_EXEC);
    print_cap_pool("RTCRAM",           MALLOC_CAP_RTCRAM);
    print_cap_pool("DEFAULT",          MALLOC_CAP_DEFAULT);
    printf("\n");

    // ------------------------------------------------------------------
    // 5. Physical heap regions
    // ------------------------------------------------------------------
    printf("PHYSICAL HEAP REGIONS (heap_caps_print_heap_info):\n");
    printf("  One entry per contiguous bank the allocator manages. Address\n");
    printf("  ranges tell you which physical memory each pool actually sits on.\n\n");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    printf("\n");

    // ------------------------------------------------------------------
    // 6. Probe allocations
    // ------------------------------------------------------------------
    printf("ALLOCATION PROBES\n");
    printf("  Attempts a real allocation of each size in each pool. Fails\n");
    printf("  when the largest free block in that pool is smaller than the\n");
    printf("  request. Reported pointer address confirms placement.\n\n");
    static const size_t sizes[] = {
        4u * 1024u,
        32u * 1024u,
        128u * 1024u,
        512u * 1024u,
        1024u * 1024u,
    };
    struct probe_pool { const char *name; uint32_t caps; };
    const struct probe_pool pools[] = {
        { "INTERNAL", MALLOC_CAP_INTERNAL },
        { "DMA",      MALLOC_CAP_DMA },
        { "SPIRAM",   MALLOC_CAP_SPIRAM },
        { "DEFAULT",  MALLOC_CAP_DEFAULT },
    };
    for (size_t i = 0; i < sizeof(pools) / sizeof(pools[0]); i++) {
        printf("  %s pool:\n", pools[i].name);
        for (size_t j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
            probe_alloc(pools[i].name, pools[i].caps, sizes[j]);
        }
        printf("\n");
    }

    // ------------------------------------------------------------------
    // 7. Address-space cheat sheet
    // ------------------------------------------------------------------
    printf("ESP32-S3 VIRTUAL ADDRESS MAP (data view unless noted)\n");
    printf("  0x3C000000 - 0x3E000000  PSRAM (up to 32 MB via cache)\n");
    printf("  0x3C000000 - 0x3E000000  Flash rodata / XIP data (shared window)\n");
    printf("  0x3FC88000 - 0x3FCF0000  Internal SRAM 1+2 (DIRAM, ~416 KB)\n");
    printf("  0x3FCF0000 - 0x3FD00000  Internal SRAM 2 tail (~64 KB)\n");
    printf("  0x3FF00000 - 0x3FF80000  Peripheral MMIO\n");
    printf("  0x40370000 - 0x403E0000  Internal SRAM instruction view (DIRAM)\n");
    printf("  0x42000000 - 0x44000000  Flash .text via instruction cache\n");
    printf("  0x50000000 - 0x50002000  RTC SLOW memory (8 KB)\n");
    printf("  0x600FE000 - 0x60100000  RTC FAST memory data view (8 KB)\n");
    printf("  0x600C0000 - 0x600C2000  RTC FAST memory instruction view\n");
    printf("\n");

    printf("======================================================\n");
    printf("           END OF MEMORY SANITY REPORT\n");
    printf("======================================================\n\n");

    // Idle forever so the report stays on the terminal.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
