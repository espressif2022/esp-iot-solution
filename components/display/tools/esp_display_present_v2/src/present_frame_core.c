/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_frame_core.h"
#include <assert.h>

esp_err_t pv2_frame_core_init(pv2_frame_core_t *c, size_t count)
{
    if (!c || !count || count > PV2_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    *c = (pv2_frame_core_t) {
        .count = count, .active = SIZE_MAX
    };
    c->frames = pv2_alloc(count * sizeof(*c->frames));
    return c->frames ? ESP_OK : ESP_ERR_NO_MEM;
}
void pv2_frame_core_deinit(pv2_frame_core_t *c)
{
    pv2_free(c->frames);
    c->frames = NULL;
}
pv2_core_frame_t *pv2_frame_core_find(pv2_frame_core_t *c, uint64_t token)
{
    return pv2_record_find(c->frames, c->count, sizeof(*c->frames), token, false);
}
pv2_core_frame_t *pv2_frame_core_find_receipt(pv2_frame_core_t *c, pv2_receipt_t receipt)
{
    return pv2_record_find(c->frames, c->count, sizeof(*c->frames), receipt.value, true);
}
esp_err_t pv2_frame_core_admit(pv2_frame_core_t *c, pv2_lock_t *lock,
                               uint32_t width, uint32_t height,
                               const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!c || !lock || !request) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = pv2_validate_damage(width, height, request->damage, request->damage_count);
    if (err != ESP_OK) {
        return err;
    }
    pv2_lock_enter(lock, false);
    bool ok = c->life == PV2_CORE_RUNNING && c->fault.result == ESP_OK;
    pv2_lock_exit(lock, false);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t pv2_frame_core_begin(pv2_frame_core_t *c, pv2_frame_t *out)
{
    out->value = 0;
    if (c->life != PV2_CORE_RUNNING || c->fault.result != ESP_OK || c->active != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < c->count; ++i) {
        if (c->frames[i].common.token) {
            continue;
        }
        uint64_t token = pv2_new_token();
        if (!token) {
            break;
        }
        c->frames[i] = (pv2_core_frame_t) {
            .common = {.token = token}
        };
        c->active = i;
        out->value = token;
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}
void PV2_ISR_ATTR pv2_frame_core_reap(pv2_frame_core_t *c, pv2_core_frame_t *f)
{
    if (!f->common.token || !f->sealed || f->pending || f->leased || f->common.external) {
        return;
    }
    if (c->active == (size_t)(f - c->frames)) {
        c->active = SIZE_MAX;
    }
    if (f->canceled) {
        pv2_cancel_publish(&c->canceled, f->common.token);
    }
    f->common.token = 0;
}
void PV2_ISR_ATTR pv2_frame_core_complete(pv2_frame_core_t *c, pv2_core_frame_t *f)
{
    assert(f->pending);
    --f->pending;
    pv2_frame_core_reap(c, f);
}
void PV2_ISR_ATTR pv2_frame_core_fail(pv2_frame_core_t *c, pv2_core_frame_t *f,
                                      uint64_t cookie, esp_err_t error)
{
    pv2_record_fail(&f->common, error);
    pv2_fault_first(&c->fault, f->common.token, cookie, error);
}
esp_err_t pv2_frame_core_seal(pv2_frame_core_t *c, pv2_core_frame_t *f, pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (c->life != PV2_CORE_RUNNING || !f || f->sealed || f->leased) {
        return ESP_ERR_INVALID_STATE;
    }
    f->sealed = true;
    c->active = SIZE_MAX;
    if (out) {
        f->common.external = true;
        out->value = f->common.token;
    }
    pv2_frame_core_reap(c, f);
    return ESP_OK;
}
esp_err_t pv2_frame_core_release(pv2_core_frame_t *f)
{
    if (!f || !f->common.external || !f->sealed || f->pending || f->leased) {
        return ESP_ERR_INVALID_STATE;
    }
    f->common.token = 0;
    return ESP_OK;
}
esp_err_t pv2_frame_core_query(pv2_frame_core_t *c, pv2_lock_t *lock, pv2_receipt_t receipt,
                               bool supports_present, pv2_frame_status_t *out)
{
    if (!c || !lock || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(lock, false);
    pv2_core_frame_t *f = pv2_frame_core_find_receipt(c, receipt);
    if (!f) {
        pv2_lock_exit(lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    bool done = f->common.terminal || (f->sealed && f->pending == 0);
    bool released = supports_present ? true : (f->common.terminal || f->pending == 0);
    *out = pv2_record_status(&f->common, f->sealed || f->common.terminal, released, done,
                             supports_present && done && f->common.result == ESP_OK);
    pv2_lock_exit(lock, false);
    return ESP_OK;
}
esp_err_t pv2_frame_core_get_fault(pv2_frame_core_t *c, pv2_lock_t *lock, pv2_backend_fault_t *out)
{
    if (!c || !lock || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(lock, false);
    *out = c->fault;
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
esp_err_t pv2_frame_core_release_receipt(pv2_frame_core_t *c, pv2_lock_t *lock, pv2_receipt_t receipt)
{
    if (!c || !lock) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(lock, false);
    pv2_core_frame_t *f = pv2_frame_core_find_receipt(c, receipt);
    esp_err_t err = pv2_frame_core_release(f);
    pv2_lock_exit(lock, false);
    return err;
}
esp_err_t pv2_frame_core_can_destroy(const pv2_frame_core_t *c)
{
    if (!c) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->life != PV2_CORE_QUIESCENT) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < c->count; ++i) {
        if (c->frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}
esp_err_t pv2_frame_core_quiescent(pv2_frame_core_t *c)
{
    if (c->life == PV2_CORE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < c->count; ++i)
        if (c->frames[i].common.token && (c->frames[i].pending || c->frames[i].leased)) {
            return ESP_ERR_TIMEOUT;
        }
    for (size_t i = 0; i < c->count; ++i) {
        pv2_frame_core_reap(c, &c->frames[i]);
    }
    c->active = SIZE_MAX;
    c->life = PV2_CORE_QUIESCENT;
    return ESP_OK;
}

void pv2_frame_core_terminal(pv2_frame_core_t *c, pv2_core_frame_t *f)
{
    f->common.terminal = true;
    f->sealed = true;
    f->pending = 0;
    pv2_frame_core_reap(c, f);
}
