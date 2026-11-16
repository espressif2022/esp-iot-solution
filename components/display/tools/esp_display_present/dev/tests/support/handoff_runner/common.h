/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef int esp_err_t;
typedef struct presenter esp_display_presenter_t;
typedef void *esp_gsp_handle_t;
typedef struct pause_token esp_gsp_esp_lcd_pause_t;
typedef struct display lv_display_t;
#define ESP_ERR_NO_MEM 0x101
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FINISHED 0x107
#define ESP_ERR_TIMEOUT 0x108
#define CONFIG_APP_PHASE_SECONDS 1
#define CONFIG_APP_HANDOFF_CYCLES 1
#define HW_LCD_H_RES 480
#define HW_LCD_V_RES 480
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(tag,fmt,...) printf(fmt "\n",##__VA_ARGS__)
#define ESP_LOGI(tag,fmt,...) ((void)0)
#define ESP_ERROR_CHECK(e) do {int error=(e);if(error){printf("ABORT:%d\n",error);exit(73);}}while(0)
const char *esp_err_to_name(int);
int64_t esp_timer_get_time(void);
void vTaskDelay(unsigned);
void lv_timer_handler(void);
void *lv_display_get_screen_active(lv_display_t *);
void lv_obj_invalidate(void *);
esp_err_t gsp_hello_load_set_value(void *, int32_t);
void *esp_gsp_timer_create(void *, unsigned, void (*)(void *, void *), void *);
esp_err_t esp_gsp_esp_lcd_pause(void *, unsigned, esp_gsp_esp_lcd_pause_t **);
esp_err_t esp_gsp_esp_lcd_resume_paused(esp_gsp_esp_lcd_pause_t *, void **);
esp_err_t esp_display_presenter_quiesce(esp_display_presenter_t *, unsigned);
esp_err_t esp_lv_present_start(esp_display_presenter_t *, lv_display_t **);
esp_err_t esp_lv_present_stop(void);
uint32_t esp_lv_present_get_frame_count(void);

esp_err_t esp_display_presenter_release_producer(esp_display_presenter_t *);
