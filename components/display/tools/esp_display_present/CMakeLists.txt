# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
include(${CMAKE_CURRENT_LIST_DIR}/src/sources.cmake)
set(_present_private_requires freertos esp_timer heap esp_mm esp_hw_support esp_driver_ppa esp_driver_dma esp_system esp_driver_gpio efuse)
set(_present_efuse_api 0)
set(_present_efuse_header "${IDF_PATH}/components/efuse/include/esp_efuse.h")
if(EXISTS "${_present_efuse_header}")
    file(READ "${_present_efuse_header}" _present_efuse_declarations)
    if(_present_efuse_declarations MATCHES "esp_efuse_is_flash_encryption_enabled[ \t\r\n]*\\(")
        set(_present_efuse_api 1)
    endif()
endif()
if(NOT _present_efuse_api)
    list(APPEND _present_private_requires bootloader_support)
endif()
idf_component_register(SRCS ${PRESENT_FACADE_SOURCES} ${PRESENT_RESOURCE_SOURCES} ${PRESENT_TE_SYNC_SOURCES} ${PRESENT_ENGINE_SOURCES}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src private_include
    REQUIRES esp_common esp_lcd
    PRIV_REQUIRES ${_present_private_requires})
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)

# SDKs expose either the older DMA2D memcpy API or public color conversion.
# The latter also performs same-format 2D copies.
set(_present_dma_header "${IDF_PATH}/components/esp_driver_dma/include/esp_private/dma2d.h")
set(_present_async_header "${IDF_PATH}/components/esp_driver_dma/include/esp_private/async_memcpy_dma2d.h")
set(_present_color_header "${IDF_PATH}/components/esp_driver_dma/include/esp_async_color_convert.h")
set(_present_alignment_api 0)
if(EXISTS "${_present_dma_header}")
    file(READ "${_present_dma_header}" _present_dma_declarations)
    if(_present_dma_declarations MATCHES "dma2d_get_buffer_alignment_constraint[ \t\r\n]*\\(")
        set(_present_alignment_api 1)
    endif()
endif()
set(_present_async_api 0)
if(EXISTS "${_present_async_header}")
    set(_present_async_api 1)
endif()
set(_present_color_api 0)
if(EXISTS "${_present_color_header}")
    set(_present_color_api 1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PRESENT_IDF_DMA2D_ALIGNMENT_API=${_present_alignment_api}
    PRESENT_IDF_ASYNC_DMA2D_API=${_present_async_api}
    PRESENT_IDF_COLOR_CONVERT_DMA2D_API=${_present_color_api}
    PRESENT_IDF_EFUSE_ENCRYPTION_API=${_present_efuse_api})

# Older DPI drivers expose refresh_done only. It is not a retirement event;
# do not substitute it for frame_buf_complete and silently weaken ownership.
set(_present_dsi_header "${IDF_PATH}/components/esp_lcd/dsi/include/esp_lcd_mipi_dsi.h")
set(_present_dsi_scanout_api 0)
if(EXISTS "${_present_dsi_header}")
    file(READ "${_present_dsi_header}" _present_dsi_declarations)
    if(_present_dsi_declarations MATCHES "on_frame_buf_complete" AND
       _present_dsi_declarations MATCHES "on_vsync")
        set(_present_dsi_scanout_api 1)
    endif()
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PRESENT_IDF_DSI_SCANOUT_API=${_present_dsi_scanout_api})

# The audited drivers ignore their cache call's result. Verify the cache
# implementation whose only hard errors are excluded by scanout_submit.
# Use resolved component paths so a replacement driver falls back to C2M.
if(IDF_TARGET STREQUAL "esp32s31" OR IDF_TARGET STREQUAL "esp32p4")
    idf_component_get_property(_present_lcd_dir esp_lcd COMPONENT_DIR)
    idf_component_get_property(_present_mm_dir esp_mm COMPONENT_DIR)
    set(_present_cache_src "${_present_mm_dir}/esp_cache_msync.c")
    if(EXISTS "${_present_cache_src}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_present_cache_src}")
        file(SHA256 "${_present_cache_src}" _present_cache_sha)
    endif()
endif()
if(_present_cache_sha STREQUAL "68faefeb975ad9c932a291fbd59215fe5a5c10b785f2c5c4d43493fad5d8c0bc")
    if(IDF_TARGET STREQUAL "esp32s31")
        # No-bounce RGB writes back a registered FB before switching DMA links.
        set(_present_rgb_src "${_present_lcd_dir}/rgb/esp_lcd_panel_rgb.c")
        if(EXISTS "${_present_rgb_src}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_present_rgb_src}")
            file(SHA256 "${_present_rgb_src}" _present_rgb_sha)
        endif()
        if(_present_rgb_sha STREQUAL "ffe52a0b4999da2e2ace1a023b17538da6766b08cee05341095ee8e64657ec11")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE PRESENT_IDF_RGB_FRAME_WRITEBACK=1)
        endif()
    elseif(IDF_TARGET STREQUAL "esp32p4" AND _present_dsi_scanout_api)
        # The ops wrapper forwards coordinates unchanged. DPI writes the
        # registered FB's clipped rows before publishing cur_fb_index.
        set(_present_dpi_src "${_present_lcd_dir}/dsi/esp_lcd_panel_dpi.c")
        set(_present_lcd_ops_src "${_present_lcd_dir}/src/esp_lcd_panel_ops.c")
        if(EXISTS "${_present_dpi_src}" AND EXISTS "${_present_lcd_ops_src}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_present_dpi_src}" "${_present_lcd_ops_src}")
            file(SHA256 "${_present_dpi_src}" _present_dpi_sha)
            file(SHA256 "${_present_lcd_ops_src}" _present_lcd_ops_sha)
            if(_present_dpi_sha STREQUAL "8b4473b5e5b3eb67421eac0b02103e3896b1e7c856b3bbb082d3f0c815b06def"
                    AND _present_lcd_ops_sha STREQUAL "0f6f0241d6ba78f9413dcf64b8afc6ee60d9278719c61a8685163c6b10af62e4")
                target_compile_definitions(${COMPONENT_LIB} PRIVATE PRESENT_IDF_DPI_FRAME_WRITEBACK=1)
            endif()
        endif()
    endif()
endif()

# Re-evaluate capabilities when SDK private/public declarations change in place.
foreach(_present_sdk_header IN ITEMS "${_present_dma_header}" "${_present_async_header}" "${_present_color_header}" "${_present_efuse_header}" "${_present_dsi_header}")
    if(EXISTS "${_present_sdk_header}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_present_sdk_header}")
    endif()
endforeach()
