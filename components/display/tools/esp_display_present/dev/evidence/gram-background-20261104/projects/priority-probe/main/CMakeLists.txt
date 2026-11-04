idf_component_register(SRCS "/tmp/gram-background-20261104/perf-priority.c" "/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/test_apps/boards/main/board_qspi.c"
INCLUDE_DIRS "/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/test_apps/boards/main"
REQUIRES heap esp_display_present esp_lv_present lvgl esp_lcd esp_timer esp_psram esp_driver_gpio esp_driver_i2c esp_lcd_co5300 esp_driver_spi efuse)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TEST_IMPL="priority" )
