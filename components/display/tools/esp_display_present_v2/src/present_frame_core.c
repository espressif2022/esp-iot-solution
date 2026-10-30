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
