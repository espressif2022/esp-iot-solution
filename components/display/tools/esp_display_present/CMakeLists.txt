# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
set(PRESENT_ENGINE_SOURCES src/present_os.c src/platform/present_te_timing.c src/platform/present_te_timer.c src/backend/present_image_repair.c src/backend/present_frame_core.c src/present_plan.c src/present_mode.c src/present_engine.c src/present_transfer_tracker.c src/present_completion_tracker.c src/pixel/present_pixel_engine.c src/backend/present_backend_gram.c
    src/pixel/executor/present_executor.c src/pixel/executor/present_executor_cpu.c src/pixel/executor/present_executor_ppa.c src/pixel/executor/present_executor_dma2d.c
    src/platform/present_lcd_spi.c src/platform/present_lcd_scanout.c src/backend/present_te_composer.c src/backend/present_backend_te.c src/backend/present_backend_fb.c)
idf_component_register(SRCS src/esp_display_presenter.c src/present_resources.c src/platform/present_te_sync.c src/present_geometry.c ${PRESENT_ENGINE_SOURCES}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    LDFRAGMENTS linker.lf
    REQUIRES esp_common esp_lcd
    PRIV_REQUIRES freertos esp_timer heap esp_mm esp_hw_support esp_driver_ppa esp_driver_dma esp_system esp_driver_gpio)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
