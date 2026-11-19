/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "platform/te/present_te_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Advanced TE admission/completion gate override (borrowed until stop).
 *
 * Task-side @c admit runs outside backend locks; ESP_ERR_TIMEOUT retries on
 * a newer edge without latching a fault. @c done is ISR-safe.
 */
typedef struct {
    esp_err_t (*admit)(void *, size_t transfer_bytes, uint64_t after_edge, uint64_t *edge); /**< Admit transfer for the current TE window. */
    void (*start)(void *);                                                                 /**< Notify transfer start. */
    void (*done)(void *, esp_err_t);                                                       /**< Notify ISR/task transfer completion or definite rejection. */
    void *user;                                                                            /**< Borrowed gate user context. */
} esp_display_present_te_gate_t;

/** @brief Normalized backend creation configuration. */
struct esp_display_present_backend_config {
    uint32_t width;                                    /**< Logical width seen by the producer. */
    uint32_t height;                                   /**< Logical height seen by the producer. */
    uint32_t tile_rows;                                /**< Input tile height in PARTITION modes. */
    size_t buffer_count;                               /**< Draw or frame buffer count (1..16). */
    size_t max_frames;                                 /**< Bounded frame/receipt record capacity (1..32). */
    esp_display_present_engine_pixel_format_t format;  /**< Pixel format. */
    esp_display_present_byte_order_t draw_order;       /**< Producer surface byte order. */
    esp_display_present_byte_order_t panel_order;      /**< Panel hardware byte order. */
    esp_display_present_engine_rotation_t rotation;    /**< Logical-to-physical clockwise rotation. */
    bool in_place;                                     /**< Legacy REGION hint; emit chooses direct swap or staging per request. */
    void *const *ext_bufs;                             /**< Optional caller/driver-owned FB array (TE/FB). */
    void *scan_buf;                                    /**< Optional member of @c ext_bufs currently scanning out. */
    size_t ext_bytes;                                  /**< Capacity in bytes of each external buffer. */
    esp_display_present_panel_adapter_t panel;         /**< Panel transport adapter (borrowed until successful stop). */
    uint32_t te_window_us;                             /**< TE only: safe transfer window from edge in us. */
    uint32_t te_budget_us;                             /**< TE only: worst-case full-frame transfer budget in us (< te_window_us). */
    esp_display_present_render_contract_t render_contract; /**< Active contract (0 selects PARTITION). */
    bool active_writes;                                /**< Permit active scanout writes (single-FB only). */
    bool async_copy;                                   /**< FB/TE only: run copy/repair asynchronously via worker/queue. */
    bool copy_on_backend_worker;                       /**< FB or single-image TE: drain copy queue on backend_worker outside engine gate. */
    esp_display_present_pixel_execute_fn copy_execute; /**< Optional custom blocking pixel executor. */
    bool (*copy_is_cpu)(void *user);                   /**< Optional current-executor query; enables inline single-tile CPU copies at 0 degrees. */
    void *copy_user;                                   /**< User context passed to @c copy_execute. */
    esp_display_present_progress_fn on_progress;       /**< Optional wake callback invoked outside locks (cannot reenter APIs). */
    void *progress_user;                               /**< User context passed to @c on_progress. */
    void (*on_work)(void *user);                       /**< Optional synchronous wake callback for external copy work or REGION READY enqueue. */
    void *work_user;                                   /**< User context passed to @c on_work. */
    bool tile_in_psram;                                /**< Prefer PSRAM for input tiles. */
    bool image_in_psram;                               /**< Prefer PSRAM for compose images. */
    bool logical_in_psram;                             /**< Prefer PSRAM for FULL logical staging canvas. */
    bool fixed_rotation;                               /**< Preserve producer dimensions across runtime rotation (physical transposes). */
    bool tx_staging;                                   /**< Legacy flag; REGION requires false (adapter owns bounded staging). */
    size_t transfer_capacity;                          /**< REGION: max in-flight transfers (0 uses tile count). */
    bool deferred_submit;                              /**< REGION: defer READY slot submission to backend_worker. */
    bool (*can_submit)(void *user);                    /**< TE: gate-held scheduling check before pin/admit. */
    void *submit_user;                                 /**< User context passed to @c can_submit. */
    esp_display_present_te_gate_t te_gate;             /**< Explicit TE gate override (zero uses native timing model). */
    esp_display_present_te_timing_t te_timing;         /**< Native TE timing parameters (period_us=0 uses fixed window). */
    size_t tile_count;                                 /**< FB PARTIAL input tile count (0 uses legacy 1). */
    size_t compose_count;                              /**< TE compose image count (0 legacy serialized 2; explicit 1/2 pool). */
};

/** @brief Return true when an FB configuration requires a conversion/repair copy path. */
static inline bool present_fb_needs_pixel_engine(const esp_display_present_backend_config_t *config)
{
    return config->render_contract != ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL ||
           config->rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || config->draw_order != config->panel_order;
}

#ifdef __cplusplus
}
#endif
