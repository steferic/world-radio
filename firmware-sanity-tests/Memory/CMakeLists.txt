# Minimal ESP-IDF project that enumerates every RAM/flash surface the chip
# exposes and reports what the ESP-IDF heap allocator can actually do with
# them. Intended as a smoke test to prove PSRAM/flash/SRAM configuration.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(memory_sanity)
