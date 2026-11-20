/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_engine.h"

/**
 * @brief Internal TE full-frame composition core (all calls serialized in task context).
 *
 * @c write() drains its borrow synchronously; @c queue_write() retains it until @c finish_write().
 */
typedef struct present_te_composer present_te_composer_t;

/** @brief Instance-scoped TE composer frame handle (0 is invalid). */
typedef struct {
    uint64_t value; /**< Frame token value. */
} present_te_frame_t;

/** @brief TE composer creation configuration. */
typedef struct {
    uint32_t width;                                    /**< Physical/logical base width in pixels. */
    uint32_t height;                                   /**< Physical/logical base height in pixels. */
    bool image_in_psram;                               /**< Allocate composition images in PSRAM when true. */
    size_t image_count;                                /**< Compose buffer count (0 selects serialized 2; explicit 1 or 2). */
    esp_display_present_engine_pixel_format_t format;  /**< Pixel format. */
    esp_display_present_byte_order_t panel_order;      /**< Target panel byte order. */
    esp_display_present_engine_rotation_t rotation;    /**< Producer write rotation (not applied to repair). */
    esp_display_present_pixel_config_t pixel;          /**< Copy queue/worker configuration (max_jobs=0 selects sync). */
} present_te_composer_config_t;

/** @brief Publish the producer-session priority to the dedicated copy worker. */
void present_te_composer_set_priority(present_te_composer_t *, unsigned);

/** @brief Execute at most one queued request in external_worker mode (join worker before stop/destroy). */
bool present_te_composer_run_one(present_te_composer_t *);

/** @brief Create a TE composer instance. */
esp_err_t present_te_composer_create(const present_te_composer_config_t *, present_te_composer_t **);
/** @brief Begin composing a new candidate frame. */
esp_err_t present_te_composer_begin(present_te_composer_t *, present_te_frame_t *);
/** @brief Synchronously copy and rotate @p src_area into the candidate compose buffer. */
esp_err_t present_te_composer_write(present_te_composer_t *, present_te_frame_t,
                                    const esp_display_present_source_t *, const esp_display_present_rect_t *src_area,
                                    uint32_t dst_x, uint32_t dst_y);
/** @brief Seal writes and advance repair until the candidate frame becomes READY (may return ESP_ERR_TIMEOUT). */
esp_err_t present_te_composer_seal(present_te_composer_t *, present_te_frame_t);
/** @brief Return a definitely rejected transmission back to READY for a newer TE edge. */
esp_err_t present_te_composer_reject(present_te_composer_t *, present_te_frame_t);
/** @brief Complete an INFLIGHT frame after all peripheral reads have ended. */
esp_err_t present_te_composer_complete(present_te_composer_t *, present_te_frame_t, esp_err_t result);
/** @brief Cancel a BUILDING/REPAIR/READY frame (INFLIGHT cannot be canceled). */
esp_err_t present_te_composer_cancel(present_te_composer_t *, present_te_frame_t);
/** @brief Destroy a stopped composer with no active/inflight frames. */
esp_err_t present_te_composer_destroy(present_te_composer_t *);

/** @brief Queue an asynchronous write; retains source buffer until finish_write() succeeds. */
esp_err_t present_te_composer_queue_write(present_te_composer_t *, present_te_frame_t,
                                          const esp_display_present_source_t *, const esp_display_present_rect_t *, uint32_t, uint32_t, esp_display_present_copy_ticket_t *);
/** @brief Poll completion and recycle an async write ticket. */
esp_err_t present_te_composer_finish_write(present_te_composer_t *, esp_display_present_copy_ticket_t, esp_err_t *);
/** @brief Stop the composer's internal copy queue within @p timeout_ms. */
esp_err_t present_te_composer_stop(present_te_composer_t *, uint32_t);
/** @brief Update logical write rotation while idle. */
esp_err_t present_te_composer_set_rotation(present_te_composer_t *, esp_display_present_engine_rotation_t);
/** @brief Pin a READY compose frame as INFLIGHT and expose its source descriptor for transmission. */
esp_err_t present_te_composer_pin(present_te_composer_t *, present_te_frame_t, esp_display_present_source_t *);
/** @brief Reinterpret drained compose buffer dimensions for a runtime rotation preserving pixel count. */
esp_err_t present_te_composer_reorient(present_te_composer_t *, esp_display_present_engine_rotation_t, uint32_t, uint32_t);
