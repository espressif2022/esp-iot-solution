/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Physical target output alignments and memory capability constraints.
 *
 * Zero alignment fields mean 1 with no implicit edge exception.
 */
typedef struct {
    uint32_t x_alignment;  /**< Physical X-origin alignment in pixels (0 means 1). */
    uint32_t y_alignment;  /**< Physical Y-origin alignment in pixels (0 means 1). */
    uint32_t w_align;      /**< Physical width alignment in pixels (0 means 1). */
    uint32_t h_align;      /**< Physical height alignment in pixels (0 means 1). */
    size_t base_alignment; /**< Minimum buffer base alignment in bytes (0 uses PRESENT_BUFFER_ALIGNMENT). */
    size_t stride_align;   /**< Physical row stride alignment in bytes (0 means 1). */
    bool tight_stride;     /**< True when packed full-width row stride is required. */
    bool full_only;        /**< True when panel requires full-frame transfers only. */
    bool ext_bufs_required;/**< True when caller/driver-owned external buffers are mandatory. */
    bool internal_dma;     /**< True when internal DMA memory is supported for transport. */
    bool ext_mem;          /**< True when external PSRAM is supported for transport. */
    size_t max_inflight;   /**< Maximum in-flight transfers (0 means unspecified). */
    size_t ext_len_align;  /**< External DMA transfer byte-length alignment (0 means 1). */
} esp_display_present_output_caps_t;
#ifdef __cplusplus
}
#endif
