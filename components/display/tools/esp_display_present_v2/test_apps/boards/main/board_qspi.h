/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "present_lcd_spi.h"
#define QSPI_TE_GPIO 43
esp_err_t qspi_board_create(esp_lcd_panel_handle_t *panel, esp_lcd_panel_io_handle_t *io);
