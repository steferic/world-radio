# Minimal ESP-IDF project that connects to Wi-Fi, issues one HTTPS GET, and
# reports the result. Intended purely as a smoke test for the network stack.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(https_sanity)
