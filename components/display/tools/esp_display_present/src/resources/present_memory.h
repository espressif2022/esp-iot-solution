/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief Allocation request accounting for framework metadata and task stacks.
 */
struct esp_display_present_backend_config;
struct esp_display_present_engine_config;
/** @brief Return metadata bytes requested per OS event object. */
size_t present_event_memory(void);
/** @brief Return metadata bytes requested per OS mutex object. */
size_t present_mutex_memory(void);
/** @brief Return metadata bytes requested per OS worker wrapper. */
size_t present_worker_memory(void);
/** @brief Return metadata bytes requested by a pixel engine with @p jobs slots. */
size_t present_pixel_memory(size_t jobs, bool async);
/** @brief Return metadata bytes requested by a copy queue with @p jobs slots. */
size_t present_copy_queue_memory(size_t jobs, bool dedicated_worker);
/** @brief Return metadata bytes requested by a TE composer instance. */
size_t present_composer_memory(size_t images, size_t jobs);
/** @brief Return metadata bytes requested by a REGION backend configuration. */
size_t present_region_memory(const struct esp_display_present_backend_config *);
/** @brief Return metadata bytes requested by a TE backend configuration. */
size_t present_te_memory(const struct esp_display_present_backend_config *);
/** @brief Return metadata bytes requested by an FB backend configuration. */
size_t present_fb_memory(const struct esp_display_present_backend_config *);
/** @brief Return metadata bytes requested by the top-level presentation engine. */
size_t present_presenter_memory(const struct esp_display_present_engine_config *);

#ifdef __cplusplus
}
#endif
