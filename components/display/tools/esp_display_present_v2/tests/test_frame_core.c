/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/present_frame_core.h"
#include <assert.h>
#include "../src/present_allocation.h"
#include "../src/present_transaction.h"

int main(void)
{
    pv2_allocation_t allocation = {0};
    pv2_allocation_hold(&allocation, PV2_REF_SCANOUT);
    assert(!pv2_allocation_write(&allocation, false, false));
    assert(pv2_allocation_write(&allocation, false, true));
    pv2_allocation_written(&allocation);
    pv2_allocation_drop(&allocation, PV2_REF_SCANOUT);
    pv2_allocation_hold(&allocation, PV2_REF_TRANSFER);
    assert(!pv2_allocation_write(&allocation, true, true));
    pv2_allocation_drop(&allocation, PV2_REF_TRANSFER);
    pv2_allocation_hold(&allocation, PV2_REF_COPY);
    assert(!pv2_allocation_write(&allocation, true, true));
    pv2_allocation_drop(&allocation, PV2_REF_COPY);
    assert(pv2_allocation_write(&allocation, false, false));
    pv2_allocation_written(&allocation);
    pv2_transaction_t tx;
    pv2_transaction_prepare(&tx);
    pv2_transaction_resolution_t decision = pv2_transaction_resolve(&tx,
    (pv2_tx_result_t) {
        .acceptance = PV2_TX_REJECTED
    });
    assert(decision.rejected && !decision.complete);
    assert(pv2_transaction_done(&tx, ESP_OK));
    assert(!pv2_transaction_done(&tx, ESP_FAIL));
    decision = pv2_transaction_resolve(&tx,
    (pv2_tx_result_t) {
        .acceptance = PV2_TX_REJECTED
    });
    assert(!decision.rejected && decision.complete && decision.result == ESP_FAIL);
    pv2_transaction_prepare(&tx);
    decision = pv2_transaction_resolve(&tx,
    (pv2_tx_result_t) {
        .acceptance = PV2_TX_ACCEPTED, .result = ESP_FAIL
    });
    assert(!decision.rejected && !decision.complete && decision.result == ESP_FAIL);
    pv2_frame_core_t c, other;
    assert(pv2_frame_core_init(&c, 1) == ESP_OK);
    assert(pv2_frame_core_init(&other, 1) == ESP_OK);
    pv2_frame_t frame, next, isolated;
    pv2_receipt_t receipt;
    assert(pv2_frame_core_begin(&c, &frame) == ESP_OK);
    assert(pv2_frame_core_begin(&other, &isolated) == ESP_OK);
    assert(frame.value != isolated.value);
    pv2_core_frame_t *f = pv2_frame_core_find(&c, frame.value);
    /* Streaming output can finish before seal without recycling the frame. */
    f->pending = 1;
    pv2_frame_core_complete(&c, f);
    assert(f->common.token == frame.value);
    f->leased = 1;
    assert(pv2_frame_core_seal(&c, f, &receipt) == ESP_ERR_INVALID_STATE);
    f->leased = 0;
    f->pending = 1;
    assert(pv2_frame_core_seal(&c, f, &receipt) == ESP_OK);
    assert(pv2_frame_core_begin(&c, &next) == ESP_ERR_NO_MEM);
    assert(pv2_frame_core_release(f) == ESP_ERR_INVALID_STATE);
    pv2_frame_core_complete(&c, f);
    assert(f->common.token == receipt.value);
    assert(pv2_frame_core_release(f) == ESP_OK);
    assert(pv2_frame_core_begin(&c, &frame) == ESP_OK);
    f = pv2_frame_core_find(&c, frame.value);
    f->pending = 1;
    assert(pv2_frame_core_seal(&c, f, NULL) == ESP_OK);
    pv2_frame_core_complete(&c, f);
    assert(!pv2_frame_core_find(&c, frame.value));
    /* Cancel remains live while closing until its final access ends. */
    assert(pv2_frame_core_begin(&c, &frame) == ESP_OK);
    f = pv2_frame_core_find(&c, frame.value);
    f->pending = 1;
    f->sealed = f->canceled = true;
    c.life = PV2_CORE_CLOSING;
    assert(pv2_frame_core_quiescent(&c) == ESP_ERR_TIMEOUT);
    assert(!pv2_cancel_matches(&c.canceled, frame.value));
    pv2_frame_core_fail(&c, f, 123, ESP_FAIL);
    pv2_frame_core_fail(&c, f, 456, ESP_ERR_INVALID_STATE);
    pv2_frame_core_complete(&c, f);
    assert(pv2_cancel_matches(&c.canceled, frame.value));
    assert(c.fault.result == ESP_FAIL && c.fault.transaction_cookie == 123);
    assert(pv2_frame_core_quiescent(&c) == ESP_OK);
    assert(pv2_frame_core_begin(&c, &next) == ESP_ERR_INVALID_STATE);
    assert(other.fault.result == ESP_OK);
    assert(pv2_frame_core_find(&other, isolated.value));
    pv2_lock_t lock;
    assert(pv2_lock_init(&lock));
    pv2_frame_request_t req = {0};
    assert(pv2_frame_core_admit(&c, &lock, 4, 4, &req, &next) == ESP_ERR_INVALID_STATE);
    assert(pv2_frame_core_admit(&other, &lock, 4, 4, &req, &next) == ESP_OK);
    assert(pv2_frame_core_can_destroy(&other) == ESP_ERR_INVALID_STATE);
    pv2_core_frame_t *of = pv2_frame_core_find(&other, isolated.value);
    of->pending = 1;
    assert(pv2_frame_core_seal(&other, of, &receipt) == ESP_OK);
    pv2_frame_status_t st;
    assert(pv2_frame_core_query(&other, &lock, receipt, true, &st) == ESP_OK);
    assert(st.sealed && st.source_released && !st.terminal && !st.present_confirmed);
    assert(pv2_frame_core_release_receipt(&other, &lock, receipt) == ESP_ERR_INVALID_STATE);
    pv2_frame_core_terminal(&other, of);
    assert(pv2_frame_core_query(&other, &lock, receipt, true, &st) == ESP_OK);
    assert(st.terminal && st.present_confirmed);
    assert(pv2_frame_core_release_receipt(&other, &lock, receipt) == ESP_OK);
    other.life = PV2_CORE_CLOSING;
    assert(pv2_frame_core_quiescent(&other) == ESP_OK);
    assert(pv2_frame_core_can_destroy(&other) == ESP_OK);
    pv2_lock_destroy(&lock);
    pv2_frame_core_deinit(&c);
    pv2_frame_core_deinit(&other);
    return 0;
}
