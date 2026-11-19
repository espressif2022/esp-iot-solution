/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdlib.h>

#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_CACHE_ALIGNED 0
#define heap_caps_malloc(size, caps) malloc((size))
#define heap_caps_calloc(count, size, caps) calloc((count), (size))
