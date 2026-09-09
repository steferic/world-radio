# Minimal ESP-IDF project that exercises every MVP input independently and
# prints a confirmation line on the monitor for each event received. Intended
# as a smoke test to prove wiring / GPIO assignments before layering the rest
# of the firmware on top.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(inputs_sanity)
