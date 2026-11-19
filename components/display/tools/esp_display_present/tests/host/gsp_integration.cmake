# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
set(gsp_adapter "${PRESENT_GSP_DIR}/components/esp_gsp_adapter")
set(gsp_runtime "${PRESENT_GSP_DIR}/src/platform/esp_idf")
set(gsp_bundle "${PRESENT_GSP_DIR}/ci/host/data/out_nav/nav.gspb")
if(EXISTS "${gsp_runtime}/gsp_esp_runtime.c" AND
   EXISTS "${PRESENT_GSP_DIR}/src/backend/esp_lcd/gsp_esp_lcd_canvas.c")
    include("${CMAKE_CURRENT_LIST_DIR}/gsp_monolithic_integration.cmake")
    return()
endif()
if(NOT EXISTS "${gsp_adapter}/src/esp_gsp_esp_lcd.c" OR
   NOT EXISTS "${gsp_runtime}/esp_gsp_idf_runtime.c" OR
   NOT EXISTS "${gsp_bundle}" OR
   NOT EXISTS "${PRESENT_GSP_DIR}/cmake/gsp_portable_targets.cmake")
    message(FATAL_ERROR "PRESENT_GSP_DIR must point to a GSP source checkout with the split adapter and Host fixtures. See tests/host/README.md.")
endif()
include("${PRESENT_GSP_DIR}/cmake/gsp_portable_targets.cmake")
gsp_add_portable_targets("${PRESENT_GSP_DIR}")
set(gsp_shim support/gsp_host)
add_executable(test_gsp_lvgl_integration test_gsp_lvgl_integration.c
    "${gsp_shim}/gsp_host_freertos.c"
    "${gsp_shim}/gsp_host_disabled.c"
    "${gsp_adapter}/src/esp_gsp_esp_lcd.c"
    "${gsp_adapter}/src/esp_gsp_display.c"
    "${gsp_runtime}/esp_gsp_idf_runtime.c")
set_source_files_properties("${gsp_runtime}/esp_gsp_idf_runtime.c" PROPERTIES COMPILE_DEFINITIONS ESP_PLATFORM=1)
target_include_directories(test_gsp_lvgl_integration PRIVATE
    "${gsp_shim}" support
    "${gsp_adapter}/include"
    "${PRESENT_GSP_DIR}/ci/host/shim")
target_link_libraries(test_gsp_lvgl_integration PRIVATE present_legacy_lvgl present_legacy gsp_portable_app m)
target_compile_options(test_gsp_lvgl_integration PRIVATE -Wall -Wextra -Werror -Wno-unused-function -UNDEBUG)
target_link_options(test_gsp_lvgl_integration PRIVATE -Wl,--wrap=gsp_memory_free
    -Wl,--wrap=esp_display_presenter_acquire_buffer)
if(PRESENT_SANITIZE)
    foreach(target IN ITEMS test_gsp_lvgl_integration gsp_portable_core gsp_portable_app gsp_project_config_host)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
    endforeach()
    target_link_options(test_gsp_lvgl_integration PRIVATE -fsanitize=address,undefined -no-pie)
endif()
add_test(NAME gsp_lvgl_integration COMMAND test_gsp_lvgl_integration "${gsp_bundle}")
set_tests_properties(gsp_lvgl_integration PROPERTIES TIMEOUT 20)
