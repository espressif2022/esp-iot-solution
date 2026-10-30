/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_presenter.h"
/* Allocation request accounting, not allocator usable-size prediction. Counts
 * framework metadata and task stacks separately from pixel/coverage storage. */
size_t pv2_event_memory(void);
size_t pv2_mutex_memory(void);
size_t pv2_worker_memory(void);
size_t pv2_pixel_memory(size_t jobs, bool async);
size_t pv2_composer_memory(void);
size_t pv2_gram_memory(const pv2_backend_config_t *);
size_t pv2_te_memory(const pv2_backend_config_t *);
size_t pv2_fb_memory(const pv2_backend_config_t *);
size_t pv2_presenter_memory(const pv2_presenter_config_t *);
