/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "backend/present_backend_types.h"
#include "backend/present_output_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_display_present_lcd_io esp_display_present_lcd_io_t;

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    esp_display_present_engine_pixel_format_t format; /* Must match the panel driver's input format. */
    esp_display_present_byte_order_t byte_order; /* Wire byte order; adapter does not convert. */
    size_t max_inflight; /* 1..16; independent of the SPI driver's queue size. */
    size_t staging_bytes; /* Bytes per staging slot; allocates two. Zero selects direct submission. */
    uint32_t staging_timeout_ms; /* Per transaction; 0 uses 1000 ms. Failure retains in-flight storage. */
    bool psram_dma_direct; /* Opt-in: underlying LCD SPI IO must enable the same flag on a capable SPI host. */
} esp_display_present_lcd_io_config_t;

/**
 * @brief SPI/QSPI IO adapter. Caller retains panel/IO ownership and guarantees:
 * - initialized, idle IO, no existing callback and exclusive use until stop;
 * - one successful draw_bitmap corresponds to exactly one FIFO color-done
 * callback, including drivers that split a color transfer internally;
 *   The panel must issue one tx_color per draw_bitmap. SPI and QSPI share
 *   the IDF SPI IO path; audited revisions and staging-band semantics are
 *   recorded in STATE_MACHINE.md, "LCD IO completion ordering (SPI/QSPI)".
 * - no other tx_color/draw_bitmap or callback registration on the same IO;
 * - format, byte order and area bounds match the panel configuration.
 *
 * Source must be 64-byte aligned. Without staging it must be DMA-capable.
 * Internal RAM is the default.
 * PSRAM direct DMA is opt-in and requires a capable SoC/SPI host, the underlying
 * esp_lcd IO psram_dma_direct flag, and a transfer length divisible by 64.
 * Cache writeback occurs before draw_bitmap. Explicit staging_bytes allocates
 * two internal DMA slots and accepts CPU-readable source memory. At most one
 * band is in DMA; the next slot can be filled concurrently. Actual wire bytes
 * are never padded to allocation alignment. Completion requires both driver
 * return and real color-done for every submitted band.
 * API task calls are serialized by caller, except nonblocking request_abort and
 * bounded quiesce may overlap submit. Driver callbacks run in ISR context;
 * staged logical completion may also be published by the submitting task.
 * Direct color-done proves DMA source retirement; the driver's eventual return
 * remains authoritative for submission errors even after an early callback.
 *
 * Staged failures before any driver submission are REJECTED with no callback,
 * so a preflight/cache failure can be retried. draw_bitmap errors are
 * UNCERTAIN, not a rollback promise. New submits then
 * stop. quiesce waits for queued source references and active callbacks before
 * unregistering, with bounded polling. If partial submission never generates
 * its final callback, quiesce returns TIMEOUT and retains all resources. This
 * adapter cannot safely abort an arbitrary panel driver; it never silently
 * clears such a transaction or calls an unbounded tx_param as a drain barrier.
 * draw_bitmap itself has the driver's blocking behavior (no timeout guarantee).
 *
 * request_abort closes future submissions and wakes staged waits, but does not
 * abort DMA. The caller must retain the compose source until logical completion.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_adapter Output adapter.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_io_create(const esp_display_present_lcd_io_config_t *config, esp_display_present_lcd_io_t **out_adapter);

/**
 * @brief Lcd spi get interface.
 *
 * @param adapter Panel adapter instance.
 * @param out_interface Output interface.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_io_get_interface(esp_display_present_lcd_io_t *adapter, esp_display_present_panel_adapter_t *out_interface);

/**
 * @brief Only before attachment or after successful backend.stop; destroy backend
 * first. Does not delete caller-owned panel or IO.
 *
 * @param adapter Panel adapter instance.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_io_destroy(esp_display_present_lcd_io_t *adapter);

/* Adapter metadata plus two aligned stages and their notification event. */
size_t esp_display_present_lcd_io_memory(size_t staging_bytes);

/* Transport constraints only; panel command alignment may be stricter. */
esp_err_t esp_display_present_lcd_io_get_output_caps(esp_display_present_lcd_io_t *adapter, esp_display_present_output_caps_t *caps);

#ifdef __cplusplus
}
#endif
