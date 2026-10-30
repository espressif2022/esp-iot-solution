# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
idf_component_register(SRCS main.c REQUIRES esp_display_present_v2 PRIV_REQUIRES esp_timer freertos)
idf_component_optional_requires(PRIVATE esp_lv_present_v2)
if(PRESENT_LVGL_DIR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PRESENT_SMOKE_LVGL=1)
endif()
