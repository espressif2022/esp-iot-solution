idf_component_register(SRCS "/tmp/present-fb-final-20261103/trace/main/boundary_trace.c" "/tmp/present-fb-final-20261103/perf_trace.c" "/tmp/present-head-stage/components/display/tools/esp_display_present/dev/test_apps/boards/main/board_s3.c"
 INCLUDE_DIRS "/tmp/present-fb-final-20261103/trace/main" "/tmp/present-head-stage/components/display/tools/esp_display_present/dev/test_apps/boards/main"
 REQUIRES esp_display_present esp_lv_present lvgl esp_lcd esp_timer esp_psram esp_driver_gpio esp_driver_i2c esp_lcd_gc9503 esp_lcd_panel_io_additions esp_io_expander_tca9554)
target_compile_definitions(${COMPONENT_LIB} PRIVATE "PROBE_IMPL=\"${PROBE_IMPL}\"")

target_compile_definitions(${COMPONENT_LIB} PRIVATE "TEST_IMPL=\"v1\"")
