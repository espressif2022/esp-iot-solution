/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "platform.h"
static inline void esp_rom_delay_us(uint32_t us)
{
    fake_time += us;
}
