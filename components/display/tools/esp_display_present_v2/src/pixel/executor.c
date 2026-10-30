/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#include <string.h>
static bool valid(const pv2_executor_t *e)
{
    return e && e->ops && e->ops->get_caps && e->ops->probe && e->ops->execute && e->ops->destroy;
}
esp_err_t pv2_executor_get_caps(const pv2_executor_t *e, pv2_executor_caps_t *caps)
{
    if (!caps) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(caps, 0, sizeof(*caps));
    return valid(e) ? e->ops->get_caps(e->context, caps) : ESP_ERR_INVALID_ARG;
}
esp_err_t pv2_executor_probe(const pv2_executor_t *e, const pv2_copy_request_t *r, pv2_executor_reason_t *reason)
{
    if (!reason) {
        return ESP_ERR_INVALID_ARG;
    }
    *reason = PV2_EXEC_INVALID_REQUEST;
    if (!valid(e)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_pixel_validate_request(r);
    return error == ESP_OK ? e->ops->probe(e->context, r, reason) : error;
}
esp_err_t pv2_executor_execute(void *context, const pv2_copy_request_t *r)
{
    pv2_executor_t *e = context;
    pv2_executor_reason_t reason;
    esp_err_t error = pv2_executor_probe(e, r, &reason);
    return error == ESP_OK ? e->ops->execute(e->context, r) : error;
}
esp_err_t pv2_executor_destroy(pv2_executor_t *e)
{
    return valid(e) ? e->ops->destroy(e->context) : ESP_ERR_INVALID_ARG;
}
esp_err_t pv2_executor_select(const pv2_executor_dispatch_t *d, const pv2_copy_request_t *r, pv2_executor_selection_t *s)
{
    if (!s) {
        return ESP_ERR_INVALID_ARG;
    }
    *s = (pv2_executor_selection_t) {
        .primary_reason = PV2_EXEC_INVALID_REQUEST
    };
    if (!d) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_executor_probe(d->primary, r, &s->primary_reason);
    if (error == ESP_OK) {
        s->executor = d->primary;
        return ESP_OK;
    }
    if (error != ESP_ERR_NOT_SUPPORTED || !d->fallback) {
        return error;
    }
    error = pv2_executor_probe(d->fallback, r, &s->fallback_reason);
    if (error == ESP_OK) {
        s->executor = d->fallback;
        s->used_fallback = true;
    }
    return error;
}
esp_err_t pv2_executor_run(const pv2_executor_dispatch_t *d, const pv2_copy_request_t *r, pv2_executor_selection_t *out)
{
    pv2_executor_selection_t local;
    pv2_executor_selection_t *s = out ? out : &local;
    esp_err_t error = pv2_executor_select(d, r, s);
    return error == ESP_OK ? pv2_executor_execute(s->executor, r) : error;
}
esp_err_t pv2_executor_dispatch_execute(void *context, const pv2_copy_request_t *r)
{
    return pv2_executor_run(context, r, NULL);
}
