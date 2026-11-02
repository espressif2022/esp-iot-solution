/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_frame_core.h"
#include <assert.h>

esp_err_t pv2_frame_core_init(pv2_frame_core_t *core, size_t count)
{
    if (!core || !count || count > PV2_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    *core = (pv2_frame_core_t) {
        .count = count, .active = SIZE_MAX
    };
    core->frames = pv2_alloc(count * sizeof(*core->frames));
    return core->frames ? ESP_OK : ESP_ERR_NO_MEM;
}
void pv2_frame_core_deinit(pv2_frame_core_t *core)
{
    pv2_free(core->frames);
    core->frames = NULL;
}
pv2_core_frame_t *pv2_frame_core_find(pv2_frame_core_t *core, uint64_t token)
{
    return pv2_record_find(core->frames, core->count, sizeof(*core->frames), token, false);
}
pv2_core_frame_t *pv2_frame_core_find_receipt(pv2_frame_core_t *core, pv2_receipt_t receipt)
{
    return pv2_record_find(core->frames, core->count, sizeof(*core->frames), receipt.value, true);
}
esp_err_t pv2_frame_core_admit(pv2_frame_core_t *core, pv2_lock_t *lock,
                               uint32_t width, uint32_t height,
                               const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!core || !lock || !request) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = pv2_validate_damage(width, height, request->damage, request->damage_count);
    if (err != ESP_OK) {
        return err;
    }
    pv2_lock_enter(lock, false);
    bool ok = core->life == PV2_CORE_RUNNING && core->fault.result == ESP_OK;
    pv2_lock_exit(lock, false);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t pv2_frame_core_begin(pv2_frame_core_t *core, pv2_frame_t *out)
{
    out->value = 0;
    if (core->life != PV2_CORE_RUNNING || core->fault.result != ESP_OK || core->active != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < core->count; ++i) {
        if (core->frames[i].common.token) {
            continue;
        }
        uint64_t token = pv2_new_token();
        if (!token) {
            break;
        }
        core->frames[i] = (pv2_core_frame_t) {
            .common = {.token = token}
        };
        core->active = i;
        out->value = token;
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}
void PV2_ISR_ATTR pv2_frame_core_reap(pv2_frame_core_t *core, pv2_core_frame_t *record)
{
    if (!record->common.token || !record->sealed || record->pending || record->leased || record->common.external) {
        return;
    }
    if (core->active == (size_t)(record - core->frames)) {
        core->active = SIZE_MAX;
    }
    if (record->canceled) {
        pv2_cancel_publish(&core->canceled, record->common.token);
    }
    record->common.token = 0;
}
void PV2_ISR_ATTR pv2_frame_core_complete(pv2_frame_core_t *core, pv2_core_frame_t *record)
{
    assert(record->pending);
    --record->pending;
    pv2_frame_core_reap(core, record);
}
void PV2_ISR_ATTR pv2_frame_core_fail(pv2_frame_core_t *core, pv2_core_frame_t *record,
                                      uint64_t cookie, esp_err_t error)
{
    pv2_record_fail(&record->common, error);
    pv2_fault_first(&core->fault, record->common.token, cookie, error);
}
esp_err_t pv2_frame_core_seal(pv2_frame_core_t *core, pv2_core_frame_t *record, pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (core->life != PV2_CORE_RUNNING || !record || record->sealed || record->leased) {
        return ESP_ERR_INVALID_STATE;
    }
    record->sealed = true;
    core->active = SIZE_MAX;
    if (out) {
        record->common.external = true;
        out->value = record->common.token;
    }
    pv2_frame_core_reap(core, record);
    return ESP_OK;
}
esp_err_t pv2_frame_core_release(pv2_core_frame_t *record)
{
    if (!record || !record->common.external || !record->sealed || record->pending || record->leased) {
        return ESP_ERR_INVALID_STATE;
    }
    record->common.token = 0;
    return ESP_OK;
}
esp_err_t pv2_frame_core_query(pv2_frame_core_t *core, pv2_lock_t *lock, pv2_receipt_t receipt,
                               bool supports_present, pv2_frame_status_t *out)
{
    if (!core || !lock || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(lock, false);
    pv2_core_frame_t *record = pv2_frame_core_find_receipt(core, receipt);
    if (!record) {
        pv2_lock_exit(lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    bool done = record->common.terminal || (record->sealed && record->pending == 0);
    bool released = supports_present ? true : (record->common.terminal || record->pending == 0);
    *out = pv2_record_status(&record->common, record->sealed || record->common.terminal, released, done,
                             supports_present && done && record->common.result == ESP_OK);
    pv2_lock_exit(lock, false);
    return ESP_OK;
}
esp_err_t pv2_frame_core_get_fault(pv2_frame_core_t *core, pv2_lock_t *lock, pv2_backend_fault_t *out)
{
    if (!core || !lock || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(lock, false);
    *out = core->fault;
    pv2_lock_exit(lock, false);
    return ESP_OK;
}
esp_err_t pv2_frame_core_wait(pv2_backend_t *backend,
                              esp_err_t (*query)(pv2_backend_t *, pv2_receipt_t, pv2_frame_status_t *),
                              pv2_receipt_t receipt, pv2_completion_point_t point,
                              bool supports_present, uint32_t timeout, pv2_frame_status_t *out)
{
    if (!backend || !query || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point == PV2_COMPLETION_PRESENT_CONFIRMED && !supports_present) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (point != PV2_COMPLETION_SOURCE_RELEASED && point != PV2_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        esp_err_t err = query(backend, receipt, out);
        if (err != ESP_OK || out->terminal) {
            return err;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}
esp_err_t pv2_frame_core_release_receipt(pv2_frame_core_t *core, pv2_lock_t *lock, pv2_receipt_t receipt)
{
    if (!core || !lock) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(lock, false);
    pv2_core_frame_t *record = pv2_frame_core_find_receipt(core, receipt);
    esp_err_t err = pv2_frame_core_release(record);
    pv2_lock_exit(lock, false);
    return err;
}
esp_err_t pv2_frame_core_can_destroy(const pv2_frame_core_t *core)
{
    if (!core) {
        return ESP_ERR_INVALID_ARG;
    }
    if (core->life != PV2_CORE_QUIESCENT) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < core->count; ++i) {
        if (core->frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}
esp_err_t pv2_frame_core_quiescent(pv2_frame_core_t *core)
{
    if (core->life == PV2_CORE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < core->count; ++i)
        if (core->frames[i].common.token && (core->frames[i].pending || core->frames[i].leased)) {
            return ESP_ERR_TIMEOUT;
        }
    for (size_t i = 0; i < core->count; ++i) {
        pv2_frame_core_reap(core, &core->frames[i]);
    }
    core->active = SIZE_MAX;
    core->life = PV2_CORE_QUIESCENT;
    return ESP_OK;
}

void pv2_frame_core_terminal(pv2_frame_core_t *core, pv2_core_frame_t *record)
{
    record->common.terminal = true;
    record->sealed = true;
    record->pending = 0;
    pv2_frame_core_reap(core, record);
}
