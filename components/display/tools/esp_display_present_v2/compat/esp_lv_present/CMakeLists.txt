# Select this directory instead of the V1 esp_lv_present component.
idf_component_register(SRCS src/legacy_lvgl.c INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS ../esp_display_present/src
    REQUIRES esp_display_present esp_lv_present_v2 lvgl PRIV_REQUIRES esp_timer esp_display_present_v2)

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
