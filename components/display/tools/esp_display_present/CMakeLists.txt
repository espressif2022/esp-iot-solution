# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
include(${CMAKE_CURRENT_LIST_DIR}/src/sources.cmake)
idf_component_register(SRCS ${PRESENT_FACADE_SOURCES} ${PRESENT_RESOURCE_SOURCES} ${PRESENT_TE_SYNC_SOURCES} ${PRESENT_ENGINE_SOURCES}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    REQUIRES esp_common esp_lcd
    PRIV_REQUIRES freertos esp_timer heap esp_mm esp_hw_support esp_driver_ppa esp_driver_dma esp_system esp_driver_gpio)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)

# Optional accelerators depend on private SDK APIs, not only SoC capability.
# Older supported SDKs keep CPU execution; never guess encrypted-buffer alignment.
set(_present_dma_header "${IDF_PATH}/components/esp_driver_dma/include/esp_private/dma2d.h")
set(_present_async_header "${IDF_PATH}/components/esp_driver_dma/include/esp_private/async_memcpy_dma2d.h")
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
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PRESENT_IDF_DMA2D_ALIGNMENT_API=${_present_alignment_api}
    PRESENT_IDF_ASYNC_DMA2D_API=${_present_async_api})

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

# Re-evaluate capabilities when SDK private/public declarations change in place.
foreach(_present_sdk_header IN ITEMS "${_present_dma_header}" "${_present_async_header}" "${_present_dsi_header}")
    if(EXISTS "${_present_sdk_header}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_present_sdk_header}")
    endif()
endforeach()
