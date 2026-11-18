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

/** @brief Opaque Panel-IO adapter handle. */
typedef struct esp_display_present_lcd_io esp_display_present_lcd_io_t;

/** @brief Panel-IO adapter creation configuration. */
typedef struct {
    esp_lcd_panel_handle_t panel;                    /**< Borrowed initialized panel handle. */
    esp_lcd_panel_io_handle_t io;                    /**< Borrowed initialized panel IO handle. */
    esp_display_present_engine_pixel_format_t format;/**< Panel driver input pixel format. */
    esp_display_present_byte_order_t byte_order;     /**< Panel wire byte order; emit converts RGB565 when needed. */
    size_t max_inflight;                             /**< Maximum in-flight transactions (1..16). */
    size_t staging_bytes;                            /**< Per-slot staging budget (two slots; REGION allocates lazily, 0 disables fallback). */
    uint32_t staging_timeout_ms;                     /**< Per-transaction staging wait budget in ms (0 uses 1000). */
    bool region_mode;                               /**< REGION emit: lazy staging, per-request direct/transform choice. */
    bool psram_dma_direct;                           /**< Opt-in direct DMA from PSRAM (must match SPI IO flag). */
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
 * Direct sources must be 64-byte aligned and DMA-capable. REGION emit selects
 * direct DMA per request, otherwise rotates/copies/swaps CPU-readable input
 * into two bounded internal slots. Source stride may exceed its pixel width.
 * Internal RAM is the default.
 * PSRAM direct DMA is opt-in and requires a capable SoC/SPI host, the underlying
 * esp_lcd IO psram_dma_direct flag, and a transfer length divisible by 64.
 * Cache writeback occurs before draw_bitmap. Explicit staging_bytes reserves
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
 * REGION emit reports driver backpressure as REJECTED/ESP_ERR_NOT_FINISHED;
 * lazy staging allocation failure is REJECTED/ESP_ERR_NO_MEM. Neither borrows
 * the source. Worker-owned OOM must fail the logical request without retrying.
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
 * abort DMA. The caller must retain the source until logical completion.
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

/** @brief Return metadata, two aligned staging buffers, and notification event bytes. */
size_t esp_display_present_lcd_io_memory(size_t staging_bytes);

/** @brief Query Panel-IO transport output alignment and memory constraints. */
esp_err_t esp_display_present_lcd_io_get_output_caps(esp_display_present_lcd_io_t *adapter, esp_display_present_output_caps_t *caps);

#ifdef __cplusplus
}
#endif
