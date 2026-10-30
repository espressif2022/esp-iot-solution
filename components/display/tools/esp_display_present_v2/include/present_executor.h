/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_pixel_engine.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Explicitly injected, statically linked executor. No global registration.
 * Ops/context are immutable for the lifetime of all borrowing engines.
 * Calls may execute concurrently: built-in executors serialize per instance.
 * Destroy is owner-serialized AFTER every borrowing engine has stopped/joined.
 * On destroy failure the executor remains valid and retryable. */
typedef struct pv2_executor pv2_executor_t;
typedef enum {
    PV2_EXEC_SUPPORTED = 0,
    PV2_EXEC_INVALID_REQUEST,
    PV2_EXEC_TRANSFORM,
    PV2_EXEC_ALIGNMENT,
    PV2_EXEC_CAPACITY,
    PV2_EXEC_LAYOUT,
    PV2_EXEC_MEMORY,
    PV2_EXEC_IN_PLACE,
} pv2_executor_reason_t;

typedef struct {
    const char *name; /* Static lifetime; identity is the executor pointer. */
    uint32_t format_mask; /* Bit indexed by pv2_pixel_format_t. Same-format only. */
    uint32_t rotation_mask;
    bool byte_swap;
    bool in_place;
    size_t base_alignment; /* Minimum; actual addresses may require more. */
    size_t destination_capacity_alignment;
    size_t stride_alignment_bytes; /* 1: no fixed byte constraint beyond pixel size. */
    uint32_t max_stride_pixels; /* 0: only checked size_t/capacity limits. */
    uint32_t max_height;
    bool address_dependent_constraints; /* probe remains mandatory. */
    bool requires_full_stride_capacity;
} pv2_executor_caps_t;

typedef struct {
    esp_err_t (*get_caps)(void *context, pv2_executor_caps_t *caps);
    /* No writes, cache maintenance, submission or allocation. Unsupported returns
     * ESP_ERR_NOT_SUPPORTED with a reason; invalid requests are NOT fallback. */
    esp_err_t (*probe)(void *context, const pv2_copy_request_t *request, pv2_executor_reason_t *reason);
    /* Returning, even on error, guarantees ALL reads/writes have ended.
     * Must revalidate requests; never fall back internally. */
    esp_err_t (*execute)(void *context, const pv2_copy_request_t *request);
    esp_err_t (*destroy)(void *context);
} pv2_executor_ops_t;
struct pv2_executor {
    const pv2_executor_ops_t *ops;
    void *context;
};

esp_err_t pv2_executor_cpu_create(pv2_executor_t **out);
esp_err_t pv2_executor_ppa_create(pv2_executor_t **out);
esp_err_t pv2_executor_dma2d_create(pv2_executor_t **out);
esp_err_t pv2_executor_get_caps(const pv2_executor_t *executor, pv2_executor_caps_t *caps);
esp_err_t pv2_executor_probe(const pv2_executor_t *executor, const pv2_copy_request_t *request, pv2_executor_reason_t *reason);
/* Direct Pixel Engine callback; context is pv2_executor_t*. */
esp_err_t pv2_executor_execute(void *context, const pv2_copy_request_t *request);
esp_err_t pv2_executor_destroy(pv2_executor_t *executor);

/* Borrows both executors. NULL fallback means strict primary-only policy.
 * Only explicit probe NOT_SUPPORTED permits selecting fallback. No retry or
 * fallback after execute starts, including errors from cache/driver operations. */
typedef struct {
    pv2_executor_t *primary;
    pv2_executor_t *fallback;
} pv2_executor_dispatch_t;
typedef struct {
    pv2_executor_t *executor; /* NULL if selection failed; not an owning reference. */
    pv2_executor_reason_t primary_reason;
    pv2_executor_reason_t fallback_reason;
    bool used_fallback;
} pv2_executor_selection_t;
/* Selection is advisory for this unchanged request, not a hardware reservation. */
esp_err_t pv2_executor_select(const pv2_executor_dispatch_t *dispatch, const pv2_copy_request_t *request, pv2_executor_selection_t *selection);
/* Per-call report avoids shared last-result races. Selected != successfully executed. */
esp_err_t pv2_executor_run(const pv2_executor_dispatch_t *dispatch, const pv2_copy_request_t *request, pv2_executor_selection_t *selection);
/* Pixel Engine callback; context is pv2_executor_dispatch_t*. */
esp_err_t pv2_executor_dispatch_execute(void *context, const pv2_copy_request_t *request);
#ifdef __cplusplus
}
#endif
