/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_presenter.h"
/* Allocation request accounting, not allocator usable-size prediction. Counts
 * framework metadata and task stacks separately from pixel/coverage storage. */
size_t present_event_memory(void);
size_t present_mutex_memory(void);
size_t present_worker_memory(void);
size_t present_pixel_memory(size_t jobs, bool async);
size_t present_composer_memory(void);
size_t present_gram_memory(const esp_display_present_backend_config_t *);
size_t present_te_memory(const esp_display_present_backend_config_t *);
size_t present_fb_memory(const esp_display_present_backend_config_t *);
size_t present_presenter_memory(const esp_display_present_engine_config_t *);
