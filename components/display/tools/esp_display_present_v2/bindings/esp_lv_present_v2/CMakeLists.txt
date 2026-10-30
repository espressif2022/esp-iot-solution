# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
if(COMMAND idf_component_register)
    idf_component_register(SRCS present_lvgl.c INCLUDE_DIRS include
        REQUIRES esp_display_present_v2)
    # External source directories and registry components use different names.
    # The application must include LVGL in COMPONENTS; do not download it here.
    idf_component_optional_requires(PUBLIC lvgl lvgl__lvgl ${PRESENT_LVGL_COMPONENT})
else()
    add_library(present_lvgl_v2 STATIC present_lvgl.c)
    target_include_directories(present_lvgl_v2 PUBLIC include)
    target_link_libraries(present_lvgl_v2 PUBLIC present_v2 lvgl)
    target_compile_options(present_lvgl_v2 PRIVATE -Wall -Wextra -Werror)
endif()
