#pragma once
#include <stddef.h>
#include "esp_err.h"

// Call once, before any TLS or heavy allocation happens.
esp_err_t mem_probe_init(void);

// Snapshot helpers you can wrap around any allocation-heavy call site.
void mem_probe_snapshot(const char *label);

// Current mbedTLS bytes outstanding + peak since boot.
size_t mem_probe_tls_bytes(void);
size_t mem_probe_tls_peak(void);