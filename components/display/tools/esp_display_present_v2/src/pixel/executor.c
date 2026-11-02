/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#include <string.h>
static bool valid(const pv2_executor_t *executor)
{
    return executor && executor->ops && executor->ops->get_caps && executor->ops->probe && executor->ops->execute && executor->ops->destroy;
}
esp_err_t pv2_executor_get_caps(const pv2_executor_t *executor, pv2_executor_caps_t *caps)
{
    if (!caps) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(caps, 0, sizeof(*caps));
    return valid(executor) ? executor->ops->get_caps(executor->context, caps) : ESP_ERR_INVALID_ARG;
}
esp_err_t pv2_executor_probe(const pv2_executor_t *executor, const pv2_copy_request_t *request, pv2_executor_reason_t *reason)
{
    if (!reason) {
        return ESP_ERR_INVALID_ARG;
    }
    *reason = PV2_EXEC_INVALID_REQUEST;
    if (!valid(executor)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_pixel_validate_request(request);
    return error == ESP_OK ? executor->ops->probe(executor->context, request, reason) : error;
}
esp_err_t pv2_executor_execute(void *context, const pv2_copy_request_t *request)
{
    pv2_executor_t *executor = context;
    pv2_executor_reason_t reason;
    esp_err_t error = pv2_executor_probe(executor, request, &reason);
    return error == ESP_OK ? executor->ops->execute(executor->context, request) : error;
}
esp_err_t pv2_executor_destroy(pv2_executor_t *executor)
{
    return valid(executor) ? executor->ops->destroy(executor->context) : ESP_ERR_INVALID_ARG;
}
esp_err_t pv2_executor_select(const pv2_executor_dispatch_t *dispatch, const pv2_copy_request_t *request, pv2_executor_selection_t *selection)
{
    if (!selection) {
        return ESP_ERR_INVALID_ARG;
    }
    *selection = (pv2_executor_selection_t) {
        .primary_reason = PV2_EXEC_INVALID_REQUEST
    };
    if (!dispatch) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_executor_probe(dispatch->primary, request, &selection->primary_reason);
    if (error == ESP_OK) {
        selection->executor = dispatch->primary;
        return ESP_OK;
    }
    if (error != ESP_ERR_NOT_SUPPORTED || !dispatch->fallback) {
        return error;
    }
    error = pv2_executor_probe(dispatch->fallback, request, &selection->fallback_reason);
    if (error == ESP_OK) {
        selection->executor = dispatch->fallback;
        selection->used_fallback = true;
    }
    return error;
}
esp_err_t pv2_executor_run(const pv2_executor_dispatch_t *dispatch, const pv2_copy_request_t *request, pv2_executor_selection_t *out)
{
    pv2_executor_selection_t local;
    pv2_executor_selection_t *selection = out ? out : &local;
    esp_err_t error = pv2_executor_select(dispatch, request, selection);
    return error == ESP_OK ? pv2_executor_execute(selection->executor, request) : error;
}
esp_err_t pv2_executor_dispatch_execute(void *context, const pv2_copy_request_t *request)
{
    return pv2_executor_run(context, request, NULL);
}
