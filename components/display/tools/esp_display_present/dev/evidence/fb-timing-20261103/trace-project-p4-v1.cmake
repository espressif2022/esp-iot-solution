cmake_minimum_required(VERSION 3.16)
set(ENV{IDF_COMPONENT_MANAGER} "0")
set(PROBE_IMPL "v1")
set(EXTRA_COMPONENT_DIRS "/tmp/present-fb-final-20261103/trace/overlay/esp_lcd" "/tmp/present-fb-final-20261103/trace/v1/esp_display_present" "/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/test_apps/boards/deps"
 "/tmp/present-head-8c69118a/reference/components/display/tools/esp_lv_present")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pv2_compat_probe)

