/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
/* P4-only memory acceptance: require actual hardware for every supported
 * internal-memory case; report external-memory fallback instead of hiding it. */
void present_board_test_executors(void)
{
    const char *tag = "esp_display_present_executor";
    esp_display_present_executor_t *cpu;
    ESP_ERROR_CHECK(esp_display_present_executor_cpu_create(&cpu));
    unsigned hw = 0, fallback = 0, cases = 0;
    for (unsigned external = 0; external < 2; external++) {
        uint32_t caps = MALLOC_CAP_8BIT | (external ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        uint8_t *src = heap_caps_aligned_alloc(128, 3072, caps);
        uint8_t *dst = heap_caps_aligned_alloc(128, 3072, caps);
        uint8_t *reference = heap_caps_aligned_alloc(128, 3072, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!src || !dst || !reference) {
            ESP_LOGE(tag, "FAIL allocation");
            abort();
        }
        for (unsigned i = 0; i < 3072; i++) {
            src[i] = (uint8_t)(i * 17 + i / 23);
        }
        for (unsigned kind = 0; kind < 2; kind++) {
            esp_display_present_executor_t *hardware;
            ESP_ERROR_CHECK(kind ? esp_display_present_executor_ppa_create(&hardware) : esp_display_present_executor_dma2d_create(&hardware));
            esp_display_present_executor_caps_t capability;
            ESP_ERROR_CHECK(esp_display_present_executor_get_caps(hardware, &capability));
            esp_display_present_executor_dispatch_t dispatch = {hardware, cpu};
            for (unsigned bpp = 2; bpp <= 3; bpp++) {
                for (unsigned rot = 0; rot < 4; rot++) {
                    esp_display_present_layout_t layout = {24, 24, 96, bpp == 2 ? ESP_DISPLAY_PRESENT_FORMAT_RGB565 : ESP_DISPLAY_PRESENT_FORMAT_RGB888, ESP_DISPLAY_PRESENT_ORDER_NATIVE};
                    esp_display_present_copy_request_t r = {.source = {src, 3072, layout}, .destination = {dst, 3072, layout},
                                                            .source_area = {1, 2, 7, 5}, .destination_x = 3, .destination_y = 4, .rotation = rot
                                                           };
                    if (kind && bpp == 2) {
                        r.destination.layout.byte_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED;
                    }
                    memset(dst, 0xa5, 3072);
                    memset(reference, 0xa5, 3072);
                    esp_display_present_copy_request_t expected = r;
                    expected.destination.pixels = reference;
                    ESP_ERROR_CHECK(esp_display_present_executor_execute(cpu, &expected));
                    esp_display_present_executor_selection_t selected;
                    ESP_ERROR_CHECK(esp_display_present_executor_select(&dispatch, &r, &selected));
                    if (!external && (kind || rot == 0) && selected.executor != hardware) {
                        ESP_LOGE(tag, "FAIL unexpected fallback kind=%u rot=%u reason=%u", kind, rot, selected.primary_reason);
                        abort();
                    }
                    /* Run through the unchanged queued Pixel Engine contract. */
                    esp_display_present_pixel_config_t pc = {.max_jobs = 1, .asynchronous = true,
                                                             .execute = esp_display_present_executor_execute, .execute_user = selected.executor
                                                            };
                    esp_display_present_pixel_engine_t *engine;
                    ESP_ERROR_CHECK(esp_display_present_pixel_create(&pc, &engine));
                    esp_display_present_copy_ticket_t ticket;
                    ESP_ERROR_CHECK(esp_display_present_pixel_submit(engine, &r, &ticket));
                    esp_display_present_copy_status_t status;
                    ESP_ERROR_CHECK(esp_display_present_pixel_wait(engine, ticket, 5000, &status));
                    ESP_ERROR_CHECK(status.result);
                    ESP_ERROR_CHECK(esp_display_present_pixel_stop(engine, 5000));
                    ESP_ERROR_CHECK(esp_display_present_pixel_release(engine, ticket));
                    ESP_ERROR_CHECK(esp_display_present_pixel_destroy(engine));
                    if (memcmp(dst, reference, 3072)) {
                        ESP_LOGE(tag, "FAIL pixels kind=%u external=%u bpp=%u rot=%u", kind, external, bpp, rot);
                        abort();
                    }
                    if (selected.executor == hardware) {
                        hw++;
                    } else {
                        fallback++;
                    }
                    cases++;
                    ESP_LOGI(tag, "PASS primary=%s external=%u bpp=%u rot=%u actual=%s reason=%u pixels=ALL",
                             capability.name, external, bpp, rot, selected.executor == hardware ? capability.name : "cpu", selected.primary_reason);
                }
            }
            ESP_ERROR_CHECK(esp_display_present_executor_destroy(hardware));
        }
        heap_caps_free(reference);
        heap_caps_free(dst);
        heap_caps_free(src);
    }
    ESP_ERROR_CHECK(esp_display_present_executor_destroy(cpu));
    if (!heap_caps_check_integrity_all(true)) {
        abort();
    }
    ESP_LOGI(tag, "EXECUTOR_SUITE_PASS cases=%u hardware=%u cpu_fallback=%u", cases, hw, fallback);
}
