idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_system
        esp_hw_support
        spi_flash
        esp_psram
        heap
)
