# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
# Compile the checkout directly, including the production canvas error path.
include("${PRESENT_GSP_DIR}/cmake/gsp_portable_targets.cmake")
gsp_add_portable_targets("${PRESENT_GSP_DIR}")
set(gsp_shim support/gsp_host)
add_executable(test_gsp_lvgl_integration test_gsp_lvgl_integration.c
    "${gsp_shim}/gsp_host_freertos.c"
    "${gsp_shim}/monolithic/gsp_host_disabled.c"
    "${gsp_runtime}/esp_gsp_esp_lcd.c"
    "${gsp_runtime}/gsp_esp_runtime.c"
    "${PRESENT_GSP_DIR}/src/backend/esp_lcd/gsp_esp_lcd_display.c"
    "${PRESENT_GSP_DIR}/src/backend/esp_lcd/gsp_esp_lcd_canvas.c")
set_source_files_properties("${gsp_runtime}/gsp_esp_runtime.c"
    PROPERTIES COMPILE_DEFINITIONS ESP_PLATFORM=1)
target_compile_definitions(test_gsp_lvgl_integration PRIVATE PRESENT_GSP_MONOLITHIC=1)
target_include_directories(test_gsp_lvgl_integration PRIVATE
    "${gsp_shim}/monolithic" "${gsp_shim}" support
    "${gsp_runtime}" "${PRESENT_GSP_DIR}/src/backend/esp_lcd"
    "${PRESENT_GSP_DIR}/src/effect")
target_link_libraries(test_gsp_lvgl_integration PRIVATE
    present_legacy_lvgl present_legacy gsp_portable_app m)
target_compile_options(test_gsp_lvgl_integration PRIVATE
    -Wall -Wextra -Werror -Wno-unused-function -UNDEBUG)
target_link_options(test_gsp_lvgl_integration PRIVATE
    -Wl,--wrap=free -Wl,--wrap=esp_display_presenter_acquire_buffer)
if(PRESENT_SANITIZE)
    foreach(target IN ITEMS test_gsp_lvgl_integration gsp_portable_core gsp_portable_app)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
    endforeach()
    target_link_options(test_gsp_lvgl_integration PRIVATE -fsanitize=address,undefined -no-pie)
endif()
add_test(NAME gsp_lvgl_integration COMMAND test_gsp_lvgl_integration "${gsp_bundle}")
set_tests_properties(gsp_lvgl_integration PROPERTIES TIMEOUT 20)
