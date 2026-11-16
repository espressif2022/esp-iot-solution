/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "handoff_recovery.h"

typedef struct {
    const void *pause;
    const void *expected_pause;
    bool lvgl_owns;
    bool quiesced;
    bool gsp_owns;
    unsigned calls[HANDOFF_RECOVERED];
} fixture_t;

static esp_err_t quiesce(void *context)
{
    fixture_t *fixture = context;
    assert(fixture->lvgl_owns && !fixture->quiesced && !fixture->gsp_owns);
    if (++fixture->calls[HANDOFF_QUIESCE_LVGL] < 3) {
        return ESP_ERR_TIMEOUT;
    }
    fixture->quiesced = true;
    return ESP_OK;
}

static esp_err_t stop(void *context)
{
    fixture_t *fixture = context;
    assert(fixture->lvgl_owns && fixture->quiesced && !fixture->gsp_owns);
    if (++fixture->calls[HANDOFF_STOP_LVGL] < 3) {
        return ESP_ERR_TIMEOUT;
    }
    fixture->lvgl_owns = false;
    return ESP_OK;
}

static esp_err_t resume(void *context)
{
    fixture_t *fixture = context;
    assert(!fixture->lvgl_owns && !fixture->gsp_owns);
    assert(fixture->pause == fixture->expected_pause);
    if (++fixture->calls[HANDOFF_RESUME_GSP] < 3) {
        return ESP_ERR_TIMEOUT;
    }
    fixture->pause = NULL;
    fixture->gsp_owns = true;
    return ESP_OK;
}

static void retries_from_failed_stage(bool lvgl_started)
{
    int token;
    fixture_t fixture = {
        .pause = &token, .expected_pause = &token, .lvgl_owns = lvgl_started,
    };
    const handoff_recovery_ops_t ops = {quiesce, stop, resume};
    handoff_recovery_stage_t stage = lvgl_started ? HANDOFF_QUIESCE_LVGL : HANDOFF_RESUME_GSP;

    /* Each failed operation models an exhausted retry group. */
    for (handoff_recovery_stage_t expected = stage; expected < HANDOFF_RECOVERED; ++expected) {
        for (unsigned failure = 0; failure < 2; ++failure) {
            assert(handoff_recover(&stage, &ops, &fixture) == ESP_ERR_TIMEOUT);
            assert(stage == expected && fixture.pause == &token);
            assert(fixture.lvgl_owns == (expected != HANDOFF_RESUME_GSP));
            assert(!fixture.gsp_owns);
            if (expected < HANDOFF_RESUME_GSP) {
                assert(fixture.calls[HANDOFF_RESUME_GSP] == 0);
            }
        }
    }
    assert(handoff_recover(&stage, &ops, &fixture) == ESP_OK);
    assert(stage == HANDOFF_RECOVERED && fixture.gsp_owns && fixture.pause == NULL);
    assert(fixture.calls[HANDOFF_QUIESCE_LVGL] == (lvgl_started ? 3u : 0u));
    assert(fixture.calls[HANDOFF_STOP_LVGL] == (lvgl_started ? 3u : 0u));
    assert(fixture.calls[HANDOFF_RESUME_GSP] == 3);
}

int main(void)
{
    retries_from_failed_stage(true);
    retries_from_failed_stage(false);
    return 0;
}
