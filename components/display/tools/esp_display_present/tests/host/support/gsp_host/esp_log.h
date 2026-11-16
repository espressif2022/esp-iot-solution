/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdio.h>

#define HOST_GSP_LOG(tag, ...) do { \
        fprintf(stderr, "%s: ", (tag)); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } while (0)
#define ESP_LOGD(tag, ...) HOST_GSP_LOG(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) HOST_GSP_LOG(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) HOST_GSP_LOG(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) HOST_GSP_LOG(tag, __VA_ARGS__)
