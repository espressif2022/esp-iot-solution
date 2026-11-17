/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_types.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Statically linked, explicitly injected pixel copy executor.
 *
 * Ops and context remain immutable while any engine borrows the executor;
 * destroy must be owner-serialized after all borrowing engines have stopped.
 */
typedef struct esp_display_present_executor esp_display_present_executor_t;

/** @brief Reason a hardware executor declined a copy request (or supported it). */
typedef enum {
    ESP_DISPLAY_PRESENT_EXEC_SUPPORTED = 0,   /**< Request is supported by this executor. */
    ESP_DISPLAY_PRESENT_EXEC_INVALID_REQUEST, /**< Copy descriptor failed validation (not fallback-eligible). */
    ESP_DISPLAY_PRESENT_EXEC_TRANSFORM,       /**< Unsupported pixel format, rotation, or byte-swap. */
    ESP_DISPLAY_PRESENT_EXEC_ALIGNMENT,       /**< Source or destination address alignment unsatisfied. */
    ESP_DISPLAY_PRESENT_EXEC_CAPACITY,        /**< Aligned destination band exceeds surface capacity. */
    ESP_DISPLAY_PRESENT_EXEC_LAYOUT,          /**< Stride or height exceeds hardware register limits. */
    ESP_DISPLAY_PRESENT_EXEC_MEMORY,          /**< Memory domain unsupported by hardware DMA. */
    ESP_DISPLAY_PRESENT_EXEC_IN_PLACE,        /**< In-place copy unsupported by this executor. */
} esp_display_present_executor_reason_t;

/** @brief Static capabilities advertised by a pixel executor implementation. */
typedef struct {
    const char *name;           /**< Static executor name string. */
    uint32_t format_mask;       /**< Bitmask indexed by esp_display_present_engine_pixel_format_t. */
    uint32_t rotation_mask;     /**< Bitmask indexed by esp_display_present_engine_rotation_t. */
    bool byte_swap;             /**< True if RGB565 byte-swap is supported. */
    bool in_place;              /**< True if in-place copy is supported. */
    size_t base_alignment;      /**< Minimum required buffer base alignment in bytes. */
    size_t dst_cap_align;       /**< Required destination capacity alignment in bytes. */
    size_t stride_align;        /**< Stride alignment in bytes (1 means pixel-size granularity). */
    uint32_t max_stride_pixels; /**< Maximum supported stride in pixels (0 means unlimited). */
    uint32_t max_height;        /**< Maximum supported height in rows (0 means unlimited). */
    bool addr_constraints;      /**< True when per-address probe() remains mandatory at runtime. */
    bool full_stride_cap;       /**< True when destination capacity must cover full-stride aligned rows. */
} esp_display_present_executor_caps_t;

/** @brief Virtual operation table for a pixel copy executor. */
typedef struct {
    esp_err_t (*get_caps)(void *context, esp_display_present_executor_caps_t *caps); /**< Query static executor caps. */
    esp_err_t (*probe)(void *context, const esp_display_present_copy_request_t *request, esp_display_present_executor_reason_t *reason); /**< Side-effect-free capability check; invalid requests must not fall back. */
    esp_err_t (*execute)(void *context, const esp_display_present_copy_request_t *request); /**< Blocking copy execution; return guarantees all memory access ended. */
    esp_err_t (*destroy)(void *context); /**< Release executor resources. */
} esp_display_present_executor_ops_t;

/** @brief Pixel executor instance binding ops table and opaque context. */
struct esp_display_present_executor {
    const esp_display_present_executor_ops_t *ops; /**< Operation table. */
    void *context;                                 /**< Implementation context. */
};

/** @brief Create a stateless CPU pixel executor. */
esp_err_t esp_display_present_executor_cpu_create(esp_display_present_executor_t **out);
/** @brief Create a hardware PPA pixel executor if supported by the target. */
esp_err_t esp_display_present_executor_ppa_create(esp_display_present_executor_t **out);
/** @brief Create a hardware DMA2D pixel executor if supported by the target. */
esp_err_t esp_display_present_executor_dma2d_create(esp_display_present_executor_t **out);
/** @brief Query static capabilities of @p executor. */
esp_err_t esp_display_present_executor_get_caps(const esp_display_present_executor_t *executor, esp_display_present_executor_caps_t *caps);
/** @brief Probe whether @p executor supports @p request without side effects. */
esp_err_t esp_display_present_executor_probe(const esp_display_present_executor_t *executor, const esp_display_present_copy_request_t *request, esp_display_present_executor_reason_t *reason);
/** @brief Pixel-engine executor adapter callback taking esp_display_present_executor_t* as @p context. */
esp_err_t esp_display_present_executor_execute(void *context, const esp_display_present_copy_request_t *request);
/** @brief Destroy an executor after all borrowing engines have stopped. */
esp_err_t esp_display_present_executor_destroy(esp_display_present_executor_t *executor);

/** @brief Primary/fallback executor pair (NULL fallback enforces strict primary-only). */
typedef struct {
    esp_display_present_executor_t *primary;  /**< Preferred executor (e.g. PPA or DMA2D). */
    esp_display_present_executor_t *fallback; /**< Optional fallback executor (typically CPU). */
} esp_display_present_executor_dispatch_t;

/** @brief Per-call executor selection and fallback diagnostic outcome. */
typedef struct {
    esp_display_present_executor_t *executor;          /**< Non-owning pointer to selected executor, or NULL. */
    esp_display_present_executor_reason_t primary_reason;  /**< Primary probe outcome. */
    esp_display_present_executor_reason_t fallback_reason; /**< Fallback probe outcome. */
    bool used_fallback;                                /**< True when fallback was selected after primary NOT_SUPPORTED. */
} esp_display_present_executor_selection_t;

/** @brief Advise which executor in @p dispatch supports @p request without reserving hardware. */
esp_err_t esp_display_present_executor_select(const esp_display_present_executor_dispatch_t *dispatch, const esp_display_present_copy_request_t *request, esp_display_present_executor_selection_t *selection);
/** @brief Probe and execute @p request using @p dispatch, returning per-call selection diagnostics. */
esp_err_t esp_display_present_executor_run(const esp_display_present_executor_dispatch_t *dispatch, const esp_display_present_copy_request_t *request, esp_display_present_executor_selection_t *selection);
/** @brief Pixel-engine dispatch callback taking esp_display_present_executor_dispatch_t* as @p context. */
esp_err_t esp_display_present_executor_dispatch_execute(void *context, const esp_display_present_copy_request_t *request);
#ifdef __cplusplus
}
#endif
