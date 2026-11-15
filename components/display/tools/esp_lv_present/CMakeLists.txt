# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
# Stable LVGL API and native binding share the presentation engine.
idf_component_register(SRCS src/esp_lv_present.c src/present_lvgl.c INCLUDE_DIRS include PRIV_INCLUDE_DIRS src
    REQUIRES esp_display_present lvgl PRIV_REQUIRES esp_timer esp_driver_ppa esp_mm)

# Same narrow IDF atomic.h workaround as the original esp_lv_present component.
idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    set(lvgl_component lvgl)
else()
    set(lvgl_component lvgl__lvgl)
endif()
idf_component_get_property(lvgl_dir ${lvgl_component} COMPONENT_DIR)
idf_component_get_property(lvgl_lib ${lvgl_component} COMPONENT_LIB)
if(TARGET ${lvgl_lib})
    set_property(SOURCE "${lvgl_dir}/src/osal/lv_freertos.c"
        TARGET_DIRECTORY ${lvgl_lib} APPEND PROPERTY COMPILE_OPTIONS -Wno-unused-function)
endif()

# The draw wrapper depends on the LVGL 9.5 software blend descriptor.
if((IDF_TARGET STREQUAL "esp32s31" OR IDF_TARGET STREQUAL "esp32p4")
        AND CONFIG_LV_CONF_SKIP AND CONFIG_LV_USE_DRAW_SW
        AND CONFIG_LV_DRAW_SW_SUPPORT_RGB565 AND CONFIG_LV_OS_FREERTOS
        AND CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT EQUAL 2 AND NOT CONFIG_LV_USE_PPA)
    file(STRINGS "${lvgl_dir}/lv_version.h" lvgl_major REGEX "^#define LVGL_VERSION_MAJOR +9$")
    file(STRINGS "${lvgl_dir}/lv_version.h" lvgl_minor REGEX "^#define LVGL_VERSION_MINOR +5$")
    if(lvgl_major AND lvgl_minor AND EXISTS "${lvgl_dir}/src/draw/sw/blend/lv_draw_sw_blend_private.h")
        target_sources(${COMPONENT_LIB} PRIVATE src/ppa_rgb565.c)
        target_include_directories(${COMPONENT_LIB} PRIVATE "${lvgl_dir}")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE PRESENT_LVGL_PPA_FILL=1)
        if(IDF_TARGET STREQUAL "esp32p4")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE PRESENT_LVGL_PPA_P4=1)
        endif()
        target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lv_draw_sw_blend_color_to_rgb565")
    endif()
endif()

idf_component_get_property(_present_dir esp_display_present COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${_present_dir}/private_include")
