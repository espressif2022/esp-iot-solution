/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_types.h"
#include "esp_display_present_renderer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque normalized backend configuration forward declaration. */
typedef struct esp_display_present_backend_config esp_display_present_backend_config_t;

/** @brief Instance-scoped active frame handle (0 is invalid). */
typedef struct {
    uint64_t value; /**< Frame token value. */
} esp_display_present_frame_t;

/** @brief Instance-scoped sealed frame receipt handle (0 is invalid). */
typedef struct {
    uint64_t value; /**< Receipt token value. */
} esp_display_present_receipt_t;

/**
 * @brief Optional area and stride override when submitting a leased buffer.
 *
 * - @c logical_area {0,0,0,0} uses the lease's acquired area; non-zero uses
 *   screen coordinates and must fit capacity/bounds (offset in FULL/DIRECT).
 * - @c stride_bytes == 0 selects tight stride (logical_area.width * bpp) for
 *   PARTITION or the lease stride for FULL/DIRECT.
 */
typedef struct {
    esp_display_present_rect_t logical_area; /**< Target logical screen area (or zero for lease default). */
    size_t stride_bytes;                     /**< Row stride in bytes (or zero for default). */
} esp_display_present_region_submit_t;

/** @brief Completion guarantee requested when waiting on a frame receipt. */
typedef enum {
    ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED,   /**< Source buffers are no longer read by copy or panel transfer. */
    ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED, /**< Panel confirmed presentation (VSYNC/scanout switch). */
} esp_display_present_completion_point_t;

/** @brief Backend frame receipt completion snapshot. */
typedef struct {
    bool sealed;            /**< True once the frame has been sealed by end()/end_ex(). */
    bool src_released;      /**< Accepted frame work drained; does not grant write access to retained scanout/baseline buffers. */
    bool present_confirmed; /**< True once presentation was confirmed by hardware scanout/VSYNC. */
    bool terminal;          /**< No further progress will occur for this frame receipt. */
    esp_err_t result;       /**< First latched error or ESP_OK. */
} esp_display_present_frame_status_t;

/** @brief Sticky first accepted-operation backend fault retained until destruction. */
typedef struct {
    esp_err_t result;              /**< First error observed (ESP_OK means no fault). */
    esp_display_present_frame_t frame; /**< Frame associated with the fault, if any. */
    uint64_t transaction_cookie;   /**< Panel transaction cookie associated with the fault, if any. */
} esp_display_present_backend_fault_t;

/**
 * @brief Adapter-normalized panel/hardware event kind.
 *
 * Platforms must define cookie correlation and retirement semantics; raw VSYNC
 * alone is not automatically a scanout retirement event.
 */
typedef enum {
    ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE,     /**< Panel DMA/IO transfer completed. */
    ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED, /**< New framebuffer scanout activation and old retirement confirmed. */
    ESP_DISPLAY_PRESENT_EVENT_TE_EDGE,           /**< Panel tearing-effect edge observed. */
    ESP_DISPLAY_PRESENT_EVENT_FAULT,             /**< Asynchronous hardware/driver fault observed. */
} esp_display_present_event_kind_t;

/** @brief Normalized event delivered from a panel adapter. */
typedef struct {
    esp_display_present_event_kind_t kind; /**< Normalized event kind. */
    uint64_t transaction_cookie;           /**< Correlated hardware transaction cookie. */
    uint64_t timestamp_us;                 /**< Event timestamp in monotonic microseconds. */
    esp_err_t result;                      /**< Event status code. */
} esp_display_present_backend_event_t;

/** @brief Driver submission acceptance state returned by panel.submit(). */
typedef enum {
    ESP_DISPLAY_PRESENT_TX_REJECTED,  /**< Not accepted; no hardware access and no callback will fire. */
    ESP_DISPLAY_PRESENT_TX_ACCEPTED,  /**< Accepted; source remains borrowed until completion or quiesce. */
    ESP_DISPLAY_PRESENT_TX_UNCERTAIN, /**< Uncertain failure; source remains borrowed until completion or quiesce. */
} esp_display_present_tx_acceptance_t;

/** @brief Return value of panel.submit() combining acceptance and error status. */
typedef struct {
    esp_display_present_tx_acceptance_t acceptance; /**< Driver acceptance status. */
    esp_err_t result;                               /**< Driver error code (ESP_OK when accepted). */
} esp_display_present_tx_result_t;

/** @brief Panel adapter event callback invoked from ISR or task context. */
typedef bool (*esp_display_present_panel_event_fn)(void *user, const esp_display_present_backend_event_t *event, bool from_isr);

/**
 * @brief Panel driver adapter interface borrowed by backends until successful stop.
 */
typedef struct {
    void *context; /**< Borrowed adapter context. */
    /**
     * @brief Register panel event callback; failure guarantees no callback registration remains.
     *
     * @param context Borrowed implementation context.
     * @param callback Completion callback retained until successful quiesce.
     * @param user Borrowed callback context.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t (*attach)(void *context, esp_display_present_panel_event_fn callback, void *user);
    /**
     * @brief Submit a pixel surface region to the panel driver.
     *
     * Region/TE panels require packed stride and retain the buffer until completion;
     * FB adapters retain the surface continuously until SCANOUT_CONFIRMED for its
     * replacement proves old retirement (or quiesce succeeds).
     *
     * @param context Borrowed implementation context.
     * @param surface Borrowed pixel surface descriptor.
     * @param area Target rectangle.
     * @param cookie Unique hardware transaction cookie.
     * @return Acceptance and result; accepted/uncertain borrows the surface.
     */
    esp_display_present_tx_result_t (*submit)(void *context, const esp_display_present_surface_t *surface, const esp_display_present_rect_t *area, uint64_t cookie);
    /**
     * @brief Drain in-flight accesses and callbacks within @p timeout_ms before unregistering.
     *
     * @param context Borrowed implementation context.
     * @param timeout_ms Total wait budget in milliseconds (0 polls, UINT32_MAX waits indefinitely).
     * @return ESP_OK on success, or ESP_ERR_TIMEOUT / error.
     */
    esp_err_t (*quiesce)(void *context, uint32_t timeout_ms);
    /** @brief Optional nonblocking, idempotent abort hint stopping future bands (never aborts DMA). */
    void (*request_abort)(void *context);
} esp_display_present_panel_adapter_t;

/** @brief Backend progress/wakeup callback invoked outside locks (ISR-aware). */
typedef bool (*esp_display_present_progress_fn)(void *user, bool from_isr);

/** @brief Opaque backend instance handle. */
typedef struct present_backend present_backend_t;

#ifdef __cplusplus
}
#endif
