/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/present_records.h"
#include <assert.h>
#include <stddef.h>

static esp_err_t attach(void *ctx, pv2_panel_event_fn callback, void *user)
{
    (void)ctx;
    (void)callback;
    (void)user;
    return ESP_OK;
}
static pv2_tx_result_t submit(void *ctx, const pv2_surface_t *surface, const pv2_rect_t *area, uint64_t cookie)
{
    (void)ctx;
    (void)surface;
    (void)area;
    (void)cookie;
    assert(false); /* No pixels submitted by this admission/cancellation test. */
    return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_FAIL
    };
}
static esp_err_t stop(void *ctx, uint32_t timeout)
{
    (void)ctx;
    (void)timeout;
    return ESP_OK;
}
static void backend_contracts(void)
{
    const pv2_backend_ops_t *all[] = {&pv2_gram_backend, &pv2_te_backend, &pv2_fb_backend};
    for (size_t i = 0; i < 3; ++i) {
        const pv2_backend_ops_t *ops = all[i];
        pv2_backend_config_t cfg = {.width = 4, .height = 4, .tile_rows = 2,
                                    .buffer_count = 2, .max_frames = 2, .format = PV2_FORMAT_RGB565,
                                    .panel = {NULL, attach, submit, stop}, .te_window_us = 1000000, .te_transfer_budget_us = 1000
                                   };
        pv2_backend_t *b;
        assert(ops->create(&cfg, &b) == ESP_OK);
        pv2_rect_t bad = {UINT32_MAX, 0, 2, 1};
        pv2_frame_request_t request = {.damage = &bad, .damage_count = 1};
        pv2_frame_t frame = {123};
        assert(ops->begin(b, &request, &frame) == ESP_ERR_INVALID_ARG && frame.value == 0);
        request = (pv2_frame_request_t) {
            0
        };
        pv2_frame_t previous = {0};
        for (unsigned j = 0; j < 8; ++j) {
            assert(ops->begin(b, &request, &frame) == ESP_OK);
            assert(frame.value && frame.value != previous.value);
            assert(ops->cancel_frame(b, frame, 0) == ESP_OK);
            assert(ops->cancel_frame(b, frame, 0) == ESP_OK);
            if (previous.value) {
                assert(ops->cancel_frame(b, previous, 0) == ESP_ERR_INVALID_STATE);
            }
            previous = frame;
        }
        pv2_frame_status_t status;
        assert(ops->query(b, (pv2_receipt_t) {
            frame.value
        }, &status) == ESP_ERR_INVALID_STATE);
        pv2_backend_fault_t fault;
        assert(ops->get_fault(b, &fault) == ESP_OK && fault.result == ESP_OK);
        assert(ops->stop(b, 0) == ESP_OK);
        assert(ops->destroy(b) == ESP_OK);
    }
}

int main(void)
{
    backend_contracts();
    /* Embedded records with backend-private tails must use the actual stride. */
    struct {
        pv2_record_t common;
        size_t pending;
    } pool[3] = {
        {{.token = 11, .external = true}, 7},
        {{.token = 22}, 8},
        {{.token = 33, .external = true}, 9}
    };
    assert(pv2_record_find(pool, 3, sizeof(pool[0]), 33, true) == &pool[2]);
    assert(!pv2_record_find(pool, 3, sizeof(pool[0]), 22, true));
    assert(pv2_record_find(pool, 3, sizeof(pool[0]), 22, false) == &pool[1]);
    assert(!pv2_record_find(pool, 3, sizeof(pool[0]), 0, false));
    pool[2].common.token = 0;
    assert(!pv2_record_find(pool, 3, sizeof(pool[0]), 33, true));
    pv2_record_fail(&pool[0].common, ESP_FAIL);
    pv2_record_fail(&pool[0].common, ESP_ERR_TIMEOUT);
    assert(pool[0].common.result == ESP_FAIL && pool[0].pending == 7);
    pv2_backend_fault_t fault = {0};
    pv2_fault_first(&fault, 1, 2, ESP_OK);
    pv2_fault_first(&fault, 11, 12, ESP_FAIL);
    pv2_fault_first(&fault, 21, 22, ESP_ERR_TIMEOUT);
    assert(fault.result == ESP_FAIL && fault.frame.value == 11 && fault.transaction_cookie == 12);
    /* Terminal failed transfer must not become successful presentation. */
    pv2_frame_status_t status = pv2_record_status(&pool[0].common, true, true, true, false);
    assert(status.terminal && status.source_released && !status.present_confirmed && status.result == ESP_FAIL);
    pv2_cancel_history_t canceled = {0};
    assert(!pv2_cancel_matches(&canceled, 0));
    pv2_cancel_publish(&canceled, 11);
    assert(pv2_cancel_matches(&canceled, 11));
    pv2_cancel_publish(&canceled, 22);
    assert(!pv2_cancel_matches(&canceled, 11) && pv2_cancel_matches(&canceled, 22));
    pv2_rect_t edge = {UINT32_MAX - 1, 0, 1, 1};
    assert(pv2_valid_rect(UINT32_MAX, 1, &edge));
    edge.width = 2;
    assert(!pv2_valid_rect(UINT32_MAX, 1, &edge));
    assert(pv2_validate_damage(10, 10, NULL, 0) == ESP_OK);
    assert(pv2_validate_damage(10, 10, NULL, 1) == ESP_ERR_INVALID_ARG);
    assert(pv2_validate_damage(10, 10, &edge, PV2_MAX_DAMAGE_AREAS + 1) == ESP_ERR_INVALID_ARG);
    uint64_t now = pv2_now_ms();
    assert(pv2_remaining_ms(now, 0) == 0);
    assert(pv2_remaining_ms(now, UINT32_MAX) == UINT32_MAX);
    assert(pv2_remaining_ms(now - 20, 10) == 0);
    return 0;
}
