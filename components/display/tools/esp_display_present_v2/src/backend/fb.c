/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_memory.h"
#include "present_backend.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_frame_core.h"
#include "present_transaction.h"
#include "present_image.h"
#include <string.h>

enum { FB_LIMIT = 3, RECORD_LIMIT = PV2_MAX_FRAMES };

/*
 * Explicit single-backend FB pipeline stages (advanced by pv2_fb_service):
 *
 * 1. FB_SLOT_PREFILL:
 *    - Entry: begin() selects a free slot when DIRECT inherits a different baseline.
 *    - Holds: writes slot.surface, reads the immutable physical baseline.
 *    - Advance: prefill copy ticket reaches terminal state -> FB_SLOT_DRAWING.
 *    - Cancel/Error: drain prefill ticket, then return slot to FREE/SCANNING.
 *
 * 2. FB_SLOT_DRAWING:
 *    - Entry: begin() (no prefill) or FB_SLOT_PREFILL completes.
 *    - Holds: writes the physical image for PARTIAL, otherwise the logical surface.
 *    - Advance: caller invokes end()/end_ex() -> FB_SLOT_DRAIN_TILE.
 *    - Cancel/Error: FB_SLOT_CANCELING drains in-flight tile copy -> FREE/SCANNING.
 *
 * 3. FB_SLOT_DRAIN_TILE:
 *    - Entry: end()/end_ex() seals producer writes.
 *    - Holds: retains each asynchronous tile source until its copy terminates.
 *    - Advance: last tile copy finishes and coverage is verified -> FB_SLOT_REPAIR.
 *    - Error: fails frame record, records sticky fault, returns slot to FREE/SCANNING.
 *
 * 4. FB_SLOT_REPAIR:
 *    - Entry: FB_SLOT_DRAIN_TILE verified coverage.
 *    - Holds: reads the physical baseline, writes history damage minus actual writes.
 *    - Advance: all required spans repaired -> FB_SLOT_CONVERT.
 *    - Error: fails frame record, records sticky fault, returns slot to FREE/SCANNING.
 *
 * 5. FB_SLOT_CONVERT:
 *    - Entry: FB_SLOT_REPAIR completes.
 *    - Holds: FULL/DIRECT read the shared logical canvas and write physical slot.scan.
 *      PARTIAL tiles already have physical orientation; no full-frame rotation.
 *    - Advance: rotation copy finishes (or 0-deg) -> publishes baseline & FB_SLOT_READY.
 *    - Error: fails frame record, records sticky fault, returns slot to FREE/SCANNING.
 *
 * 6. FB_SLOT_READY:
 *    - Entry: candidate is fully prepared and immutable (baseline published).
 *    - Holds: slot.surface (as baseline) and physical scanout target.
 *    - Advance: pending switch slot is idle -> panel.submit -> FB_SLOT_PENDING.
 *    - Error: prior channel fault aborts queued READY frames -> FREE.
 *
 * 7. FB_SLOT_SUBMITTING / FB_SLOT_PENDING:
 *    - Entry: panel.submit called / accepted.
 *    - Holds: hardware may read both old scanning slot and new pending slot.
 *    - Advance: matching PV2_EVENT_SCANOUT_CONFIRMED -> retires old scanning slot
 *      to FB_SLOT_FREE and promotes pending slot to FB_SLOT_SCANNING.
 *
 * 8. FB_SLOT_SCANNING:
 *    - Entry: initial_scanout_buffer or confirmed switch.
 *    - Holds: continuous scanout borrow until replaced by a confirmed newer frame
 *      or stopped via panel.quiesce.
 */
typedef enum {
    FB_SLOT_FREE,
    FB_SLOT_PREFILL,
    FB_SLOT_DRAWING,
    FB_SLOT_CANCELING,
    FB_SLOT_DRAIN_TILE,
    FB_SLOT_REPAIR,
    FB_SLOT_CONVERT,
    FB_SLOT_READY,
    FB_SLOT_SUBMITTING,
    FB_SLOT_PENDING,
    FB_SLOT_SCANNING,
} fb_state_t;

typedef struct {
    pv2_surface_t surface;
    pv2_surface_t scan;
    bool draw_owned;
    bool scan_owned;
    fb_state_t state;
    size_t record;
    uint64_t sequence;
    uint64_t cookie;
    bool confirmed;
    pv2_transaction_t transaction;
} fb_slot_t;

typedef pv2_core_frame_t fb_record_t;

typedef struct {
    void *pixels;
    pv2_lease_t lease;
    pv2_copy_ticket_t copy;
    pv2_rect_t area;
} fb_tile_t;

struct pv2_backend {
    pv2_lock_t lock;
    pv2_backend_config_t config;
    pv2_pixel_engine_t *pixel;
    fb_slot_t slots[FB_LIMIT];
    pv2_frame_core_t core;
    size_t active;
    size_t pending;
    size_t scanning;
    size_t baseline;
    size_t bytes;
    size_t tile_bytes;
    pv2_image_builder_t image;
    fb_tile_t *tiles;
    size_t tile_count;
    pv2_lease_t lease;
    pv2_copy_ticket_t copy;
    pv2_rect_t copy_area;
    bool copy_is_tile;
    size_t repair_pos;
    bool scan_storage;
    bool converted;
    uint64_t sequence;

    bool submitted;

};

static bool valid_area(pv2_backend_t *b, const pv2_rect_t *a)
{
    return pv2_valid_rect(b->config.width, b->config.height, a);
}

static void PV2_ISR_ATTR fault(pv2_backend_t *b, size_t index, esp_err_t error)
{
    if (error == ESP_OK) {
        return;
    }
    uint64_t token = 0, cookie = 0;
    if (index != SIZE_MAX) {
        fb_slot_t *slot = &b->slots[index];
        fb_record_t *record = &b->core.frames[slot->record];
        token = record->common.token;
        cookie = slot->cookie;
        pv2_record_fail(&record->common, error);
    }
    pv2_fault_first(&b->core.fault, token, cookie, error);
}

static bool PV2_ISR_ATTR dispatch(void *context, const pv2_backend_event_t *event, bool isr)
{
    pv2_backend_t *b = context;
    if (!b || !event) {
        return false;
    }
    pv2_lock_enter(&b->lock, isr);
    if ((b->core.life != PV2_CORE_QUIESCENT)) {
        if (event->kind == PV2_EVENT_FAULT && !event->transaction_cookie) {
            fault(b, b->pending, event->result == ESP_OK ? ESP_FAIL : event->result);
        } else if (b->pending != SIZE_MAX) {
            fb_slot_t *slot = &b->slots[b->pending];
            if (slot->cookie && slot->cookie == event->transaction_cookie) {
                if (event->kind == PV2_EVENT_SCANOUT_CONFIRMED) {
                    if (!pv2_transaction_done(&slot->transaction, event->result)) {
                        fault(b, b->pending, ESP_ERR_INVALID_STATE);
                    }
                    if (event->result == ESP_OK) {
                        slot->confirmed = true;
                    } else {
                        fault(b, b->pending, event->result);
                    }
                } else if (event->kind == PV2_EVENT_FAULT) {
                    fault(b, b->pending, event->result == ESP_OK ? ESP_FAIL : event->result);
                }
            }
        }
    }
    pv2_lock_exit(&b->lock, isr);
    return false;
}

static bool PV2_ISR_ATTR fb_on_event_isr(pv2_backend_t *b, const pv2_backend_event_t *event)
{
    return dispatch(b, event, true);
}

static void fb_on_event(pv2_backend_t *b, const pv2_backend_event_t *event)
{
    (void)dispatch(b, event, false);
}

/* Lock held. Receipt completion is independent of framebuffer retirement. */
static void terminal(pv2_backend_t *b, size_t index)
{
    fb_record_t *record = &b->core.frames[b->slots[index].record];
    pv2_frame_core_terminal(&b->core, record);
}

static void cover(pv2_backend_t *b, pv2_rect_t area)
{
    pv2_image_cover(&b->image, area);
}

/* Reconstruct a logical region from the immutable physical baseline. FULL /
 * DIRECT writers share one logical canvas; physical images never alias it. */
static pv2_copy_request_t baseline_copy(pv2_backend_t *b, size_t target, pv2_rect_t area)
{
    pv2_surface_t source = b->scan_storage ? b->slots[b->baseline].scan : b->slots[b->baseline].surface;
    pv2_rect_t source_area = area;
    pv2_rotation_t rotation = PV2_ROTATE_0;
    if (b->scan_storage) {
        (void)pv2_rotate_rect(b->config.width, b->config.height, b->config.rotation, &area, &source_area);
        rotation = (4 - b->config.rotation) % 4;
    }
    return (pv2_copy_request_t) {
        .source = {source.pixels, source.capacity_bytes, source.layout},
        .destination = b->slots[target].surface, .source_area = source_area,
        .destination_x = area.x, .destination_y = area.y, .rotation = rotation
    };
}

static bool copy_done(void *context, pv2_copy_ticket_t ticket, const pv2_copy_status_t *status, bool isr)
{
    (void)ticket;
    (void)status;
    pv2_backend_t *b = context;
    return b->config.on_progress ? b->config.on_progress(b->config.progress_user, isr) : false;
}

static bool tiles_pending(const pv2_backend_t *b)
{
    for (size_t i = 0; i < b->tile_count; i++)
        if (b->tiles[i].copy.value) {
            return true;
        }
    return false;
}
static bool tiles_leased(const pv2_backend_t *b)
{
    for (size_t i = 0; i < b->tile_count; i++)
        if (b->tiles[i].lease.id.value) {
            return true;
        }
    return false;
}
static pv2_lease_t *find_draw_lease(pv2_backend_t *b, uint64_t token, fb_tile_t **tile)
{
    *tile = NULL;
    if (!token) {
        return NULL;
    }
    if (!b->tile_count) {
        return b->lease.id.value == token ? &b->lease : NULL;
    }
    for (size_t i = 0; i < b->tile_count; i++)
        if (b->tiles[i].lease.id.value == token) {
            *tile = &b->tiles[i];
            return &b->tiles[i].lease;
        }
    return NULL;
}

/* Owner task only. Scratch and candidate remain pinned through terminal copy. */
static void progress_copy(pv2_backend_t *b)
{
    for (size_t i = 0; i < b->tile_count; i++) {
        fb_tile_t *tile = &b->tiles[i];
        if (!tile->copy.value) {
            continue;
        }
        pv2_copy_status_t status;
        if (pv2_pixel_wait(b->pixel, tile->copy, 0, &status) != ESP_OK) {
            continue;
        }
        (void)pv2_pixel_release(b->pixel, tile->copy);
        tile->copy.value = 0;
        if (status.result == ESP_OK) {
            cover(b, tile->area);
            b->submitted = true;
        } else {
            pv2_lock_enter(&b->lock, false);
            fault(b, b->active, status.result);
            pv2_lock_exit(&b->lock, false);
        }
    }
    if (!b->copy.value) {
        return;
    }
    pv2_copy_status_t status;
    if (pv2_pixel_wait(b->pixel, b->copy, 0, &status) != ESP_OK) {
        return;
    }
    (void)pv2_pixel_release(b->pixel, b->copy);
    b->copy.value = 0;
    if (status.result == ESP_OK) {
        if (b->copy_is_tile) {
            cover(b, b->copy_area);
            b->submitted = true;
        } else if (b->active != SIZE_MAX && b->slots[b->active].state == FB_SLOT_PREFILL) {
            b->slots[b->active].state = FB_SLOT_DRAWING;
        }
    } else {
        pv2_lock_enter(&b->lock, false);
        fault(b, b->active, status.result);
        pv2_lock_exit(&b->lock, false);
    }
}

static void fail_active_build(pv2_backend_t *b, esp_err_t error)
{
    pv2_lock_enter(&b->lock, false);
    size_t idx = b->active;
    pv2_image_cancel(&b->image);
    fault(b, idx, error);
    terminal(b, idx);
    b->slots[idx].state = idx == b->scanning ? FB_SLOT_SCANNING : FB_SLOT_FREE;
    if (b->config.buffer_count == 1) {
        b->baseline = SIZE_MAX;
    }
    b->active = SIZE_MAX;
    pv2_lock_exit(&b->lock, false);
}

/*
 * Advance post-build stages for b->active:
 *   FB_SLOT_CANCELING -> FREE / SCANNING
 *   FB_SLOT_DRAIN_TILE -> FB_SLOT_REPAIR -> FB_SLOT_CONVERT -> FB_SLOT_READY
 */
/* Internal progress is independent of ESP operation errors: a hardware TIMEOUT
 * must never be interpreted as successful asynchronous acceptance. */
typedef enum { FB_ADVANCE_IDLE, FB_ADVANCE_PENDING, FB_ADVANCE_COMPLETE, FB_ADVANCE_FAILED } fb_advance_state_t;
typedef struct {
    fb_advance_state_t state;
    esp_err_t error;
} fb_advance_result_t;
/* ATTEMPT can reject end() and leave the producer frame open. Once accepted,
 * failures must terminate its receipt and retain a sticky fault. */
typedef enum { FB_SEAL_ATTEMPT, FB_SEAL_ACCEPTED } fb_seal_context_t;

static fb_advance_result_t advance_build_pipeline(pv2_backend_t *b, fb_seal_context_t context)
{
    progress_copy(b);
    if (b->active == SIZE_MAX) {
        return (fb_advance_result_t) {
            FB_ADVANCE_IDLE, ESP_OK
        };
    }
    fb_slot_t *slot = &b->slots[b->active];
    if (slot->state == FB_SLOT_CANCELING) {
        if (b->copy.value || tiles_pending(b)) {
            return (fb_advance_result_t) {
                FB_ADVANCE_PENDING, ESP_OK
            };
        }
        pv2_lock_enter(&b->lock, false);
        size_t index = b->active;
        pv2_image_cancel(&b->image);
        uint64_t token = b->core.frames[slot->record].common.token;
        b->core.frames[slot->record].common.token = 0;
        slot->state = index == b->scanning ? FB_SLOT_SCANNING : FB_SLOT_FREE;
        if (b->config.buffer_count == 1) {
            b->baseline = SIZE_MAX;
        }
        b->lease.id.value = 0;
        b->active = SIZE_MAX;
        pv2_cancel_publish(&b->core.canceled, token);
        pv2_lock_exit(&b->lock, false);
        return (fb_advance_result_t) {
            FB_ADVANCE_COMPLETE, ESP_OK
        };
    }
    if ((b->core.life != PV2_CORE_RUNNING) || slot->state == FB_SLOT_PREFILL || slot->state == FB_SLOT_DRAWING) {
        return (fb_advance_result_t) {
            FB_ADVANCE_IDLE, ESP_OK
        };
    }

    if (b->copy.value || tiles_pending(b)) {
        return (fb_advance_result_t) {
            FB_ADVANCE_PENDING, ESP_OK
        };
    }

    pv2_lock_enter(&b->lock, false);
    esp_err_t error = b->core.fault.result;
    pv2_lock_exit(&b->lock, false);
    if (error != ESP_OK) {
        if (context == FB_SEAL_ACCEPTED) {
            fail_active_build(b, error);
        }
        return (fb_advance_result_t) {
            FB_ADVANCE_FAILED, error
        };
    }

    /* Stage 3 -> 4: FB_SLOT_DRAIN_TILE -> FB_SLOT_REPAIR */
    if (slot->state == FB_SLOT_DRAIN_TILE) {
        bool require_full = b->baseline == SIZE_MAX || b->config.render_contract == PV2_CONTRACT_FULL;
        if (require_full && !pv2_image_full(&b->image)) {
            error = ESP_ERR_INVALID_STATE;
            if (context == FB_SEAL_ACCEPTED) {
                fail_active_build(b, error);
            }
            return (fb_advance_result_t) {
                FB_ADVANCE_FAILED, error
            };
        }
        if (!b->submitted) {
            error = ESP_ERR_INVALID_STATE;
            if (context == FB_SEAL_ACCEPTED) {
                fail_active_build(b, error);
            }
            return (fb_advance_result_t) {
                FB_ADVANCE_FAILED, error
            };
        }
        slot->state = FB_SLOT_REPAIR;
    }

    /* Stage 4 -> 5: FB_SLOT_REPAIR -> FB_SLOT_CONVERT */
    if (slot->state == FB_SLOT_REPAIR) {
        if (b->config.render_contract != PV2_CONTRACT_FULL &&
                b->baseline != SIZE_MAX && b->baseline != b->active) {
            pv2_image_prepare_repair(&b->image, b->config.render_contract == PV2_CONTRACT_DIRECT);
            pv2_rect_t area;
            while (pv2_image_next_repair(&b->image, &area)) {
                pv2_copy_request_t copy = baseline_copy(b, b->active, area);
                if (!b->config.async_copy) {
                    error = pv2_pixel_execute_cpu(&copy);
                    if (error != ESP_OK) {
                        if (context == FB_SEAL_ACCEPTED) {
                            fail_active_build(b, error);
                        }
                        return (fb_advance_result_t) {
                            FB_ADVANCE_FAILED, error
                        };
                    }
                    pv2_image_repaired(&b->image, &area);
                    continue;
                }
                error = pv2_pixel_submit(b->pixel, &copy, &b->copy);
                if (error != ESP_OK) {
                    if (context == FB_SEAL_ACCEPTED) {
                        fail_active_build(b, error);
                    }
                    return (fb_advance_result_t) {
                        FB_ADVANCE_FAILED, error
                    };
                }
                b->copy_is_tile = false;
                pv2_image_repaired(&b->image, &area);
                return (fb_advance_result_t) {
                    FB_ADVANCE_PENDING, ESP_OK
                };
            }
        }
        slot->state = FB_SLOT_CONVERT;
    }

    /* Stage 5 -> 6: FB_SLOT_CONVERT -> FB_SLOT_READY */
    if (slot->state == FB_SLOT_CONVERT) {
        if (b->scan_storage && !b->converted) {
            pv2_surface_t source = slot->surface;
            pv2_copy_request_t copy = {
                .source = {source.pixels, source.capacity_bytes, source.layout},
                .destination = slot->scan,
                .source_area = {0, 0, b->config.width, b->config.height},
                .rotation = b->config.rotation
            };
            error = pv2_pixel_submit(b->pixel, &copy, &b->copy);
            if (error != ESP_OK) {
                if (context == FB_SEAL_ACCEPTED) {
                    fail_active_build(b, error);
                }
                return (fb_advance_result_t) {
                    FB_ADVANCE_FAILED, error
                };
            }
            b->copy_is_tile = false;
            b->converted = true;
            if (b->config.async_copy) {
                return (fb_advance_result_t) {
                    FB_ADVANCE_PENDING, ESP_OK
                };
            }
            progress_copy(b);
            pv2_lock_enter(&b->lock, false);
            error = b->core.fault.result;
            pv2_lock_exit(&b->lock, false);
            if (error != ESP_OK) {
                if (context == FB_SEAL_ACCEPTED) {
                    fail_active_build(b, error);
                }
                return (fb_advance_result_t) {
                    FB_ADVANCE_FAILED, error
                };
            }
        }
        if (b->sequence == UINT64_MAX) {
            error = ESP_ERR_NO_MEM;
            if (context == FB_SEAL_ACCEPTED) {
                fail_active_build(b, error);
            }
            return (fb_advance_result_t) {
                FB_ADVANCE_FAILED, error
            };
        }
        pv2_lock_enter(&b->lock, false);
        (void)pv2_image_commit(&b->image);
        slot->sequence = ++b->sequence;
        slot->state = FB_SLOT_READY;
        b->baseline = b->active;
        b->active = SIZE_MAX;
        pv2_lock_exit(&b->lock, false);
        return (fb_advance_result_t) {
            FB_ADVANCE_COMPLETE, ESP_OK
        };
    }
    return (fb_advance_result_t) {
        FB_ADVANCE_IDLE, ESP_OK
    };
}

esp_err_t pv2_fb_service(pv2_backend_t *b)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)advance_build_pipeline(b, FB_SEAL_ACCEPTED);
    pv2_lock_enter(&b->lock, false);
    if (b->pending != SIZE_MAX && b->slots[b->pending].state == FB_SLOT_PENDING && b->slots[b->pending].confirmed) {
        size_t next = b->pending;
        if (b->scanning != SIZE_MAX && b->scanning != next) {
            pv2_allocation_drop(&b->image.allocations[b->scanning], PV2_REF_SCANOUT);
            b->slots[b->scanning].state = FB_SLOT_FREE;
        }
        pv2_allocation_drop(&b->image.allocations[next], PV2_REF_TRANSFER);
        if (b->scanning != next) {
            pv2_allocation_hold(&b->image.allocations[next], PV2_REF_SCANOUT);
        }
        b->slots[next].state = FB_SLOT_SCANNING;
        b->scanning = next;
        b->pending = SIZE_MAX;
        terminal(b, next);
    }
    if (b->core.fault.result != ESP_OK) {
        for (size_t i = 0; i < b->config.buffer_count; i++) {
            if (b->slots[i].state == FB_SLOT_READY) {
                fault(b, i, b->core.fault.result);
                terminal(b, i);
                b->slots[i].state = FB_SLOT_FREE;
            }
        }
    }
    if ((b->core.life != PV2_CORE_RUNNING) || b->pending != SIZE_MAX || b->core.fault.result != ESP_OK) {
        pv2_lock_exit(&b->lock, false);
        return ESP_OK;
    }
    size_t next = SIZE_MAX;
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        if (b->slots[i].state == FB_SLOT_READY && (next == SIZE_MAX || b->slots[i].sequence < b->slots[next].sequence)) {
            next = i;
        }
    }
    if (next == SIZE_MAX) {
        pv2_lock_exit(&b->lock, false);
        return ESP_OK;
    }
    fb_slot_t *slot = &b->slots[next];
    slot->cookie = pv2_new_token();
    if (!slot->cookie) {
        pv2_lock_exit(&b->lock, false);
        return ESP_ERR_NO_MEM;
    }
    slot->confirmed = false;
    pv2_transaction_prepare(&slot->transaction);
    pv2_allocation_hold(&b->image.allocations[next], PV2_REF_TRANSFER);
    slot->state = FB_SLOT_SUBMITTING;
    b->pending = next;
    pv2_lock_exit(&b->lock, false);
    const pv2_surface_t *surface = b->scan_storage ? &slot->scan : &slot->surface;
    pv2_rect_t area = {0, 0, surface->layout.width, surface->layout.height};
    pv2_tx_result_t tx = b->config.panel.submit(b->config.panel.context, surface, &area, slot->cookie);
    pv2_lock_enter(&b->lock, false);
    pv2_transaction_resolution_t resolved = pv2_transaction_resolve(&slot->transaction, tx);
    if (resolved.rejected) {
        pv2_allocation_drop(&b->image.allocations[next], PV2_REF_TRANSFER);
        slot->state = FB_SLOT_READY;
        slot->cookie = 0;
        b->pending = SIZE_MAX;
        pv2_lock_exit(&b->lock, false);
        return tx.result == ESP_OK ? ESP_FAIL : tx.result;
    }
    fault(b, next, resolved.result);
    slot->state = FB_SLOT_PENDING;
    /* Early confirmation is consumed by the next pump, not recursively sending. */
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static void free_backend(pv2_backend_t *b)
{
    if (b->pixel) {
        (void)pv2_pixel_stop(b->pixel, UINT32_MAX);
    }
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        if (b->slots[i].draw_owned) {
            pv2_free(b->slots[i].surface.pixels);
        }
        if (b->slots[i].scan_owned) {
            pv2_free(b->slots[i].scan.pixels);
        }
    }
    if (b->pixel) {
        (void)pv2_pixel_stop(b->pixel, 0);
        (void)pv2_pixel_destroy(b->pixel);
    }
    pv2_frame_core_deinit(&b->core);
    if (b->tiles)
        for (size_t i = 0; i < b->tile_count; i++) {
            pv2_free(b->tiles[i].pixels);
        }
    pv2_free(b->tiles);
    pv2_image_deinit(&b->image);
    pv2_lock_destroy(&b->lock);
    pv2_free(b);
}

static esp_err_t fb_create(const pv2_backend_config_t *c, pv2_backend_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (c && c->copy_execute && !c->async_copy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!c || !c->panel.attach || !c->panel.submit || !c->panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->tile_buffer_count > PV2_MAX_TILE_BUFFERS || !c->width || !c->height || !c->buffer_count || c->buffer_count > FB_LIMIT ||
            !c->max_frames || c->max_frames > RECORD_LIMIT) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((c->format != PV2_FORMAT_RGB565 && c->format != PV2_FORMAT_RGB888) ||
            (c->draw_order != PV2_ORDER_NATIVE && c->draw_order != PV2_ORDER_SWAPPED) ||
            (c->panel_order != PV2_ORDER_NATIVE && c->panel_order != PV2_ORDER_SWAPPED) ||
            (c->format == PV2_FORMAT_RGB888 && (c->draw_order != PV2_ORDER_NATIVE || c->panel_order != PV2_ORDER_NATIVE))) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((c->rotation < PV2_ROTATE_0 || c->rotation > PV2_ROTATE_270) || c->in_place_convert ||
            c->render_contract > PV2_CONTRACT_DIRECT || c->render_contract < PV2_CONTRACT_PARTITION ||
            (c->buffer_count == 1 && !c->allow_active_scanout_writes)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->render_contract == PV2_CONTRACT_PARTITION && (!c->tile_rows || c->tile_rows > c->height)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = c->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (c->width > SIZE_MAX / c->height || (size_t)c->width * c->height > (SIZE_MAX - (PV2_BUFFER_ALIGNMENT - 1)) / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t pixels = (size_t)c->width * c->height, bytes = pixels * bpp;
    if ((!c->external_buffers && c->external_buffer_bytes) ||
            (c->external_buffers && c->external_buffer_bytes < bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->external_buffers) {
        for (size_t i = 0; i < c->buffer_count; i++) {
            uintptr_t p = (uintptr_t)c->external_buffers[i];
            if (!p || p % PV2_BUFFER_ALIGNMENT || p > UINTPTR_MAX - c->external_buffer_bytes) {
                return ESP_ERR_INVALID_ARG;
            }
            for (size_t j = 0; j < i; j++) {
                uintptr_t q = (uintptr_t)c->external_buffers[j];
                if (p < q + c->external_buffer_bytes && q < p + c->external_buffer_bytes) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }
    pv2_backend_t *b = pv2_alloc(sizeof(*b));
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&b->lock)) {
        pv2_free(b);
        return ESP_ERR_NO_MEM;
    }
    b->config = *c;
    if (pv2_frame_core_init(&b->core, c->max_frames) != ESP_OK) {
        free_backend(b);
        return ESP_ERR_NO_MEM;
    }
    b->bytes = bytes;
    b->tile_bytes = c->render_contract == PV2_CONTRACT_PARTITION ? (size_t)c->width * c->tile_rows * bpp : bytes;
    b->active = b->pending = b->scanning = b->baseline = SIZE_MAX;
    bool quarter = c->rotation == PV2_ROTATE_90 || c->rotation == PV2_ROTATE_270;
    bool physical_partial = c->render_contract == PV2_CONTRACT_PARTITION;
    esp_err_t image_error = pv2_image_init(&b->image,
                                           physical_partial && quarter ? c->height : c->width,
                                           physical_partial && quarter ? c->width : c->height);
    b->image.allow_scanout_writes = c->allow_active_scanout_writes;
    if (c->render_contract == PV2_CONTRACT_PARTITION) {
        b->tile_count = c->tile_buffer_count ? c->tile_buffer_count : 1;
        b->tiles = pv2_alloc(b->tile_count * sizeof(*b->tiles));
        if (!b->tiles) {
            free_backend(b);
            return ESP_ERR_NO_MEM;
        }
        for (size_t i = 0; i < b->tile_count; i++) {
            b->tiles[i].pixels = pv2_alloc_pixels_in(b->tile_bytes, c->tile_in_psram);
            if (!b->tiles[i].pixels) {
                free_backend(b);
                return ESP_ERR_NO_MEM;
            }
        }
    }
    if (image_error != ESP_OK) {
        free_backend(b);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < c->buffer_count; i++) {
        b->slots[i].surface = (pv2_surface_t) {
            c->external_buffers ? c->external_buffers[i] : pv2_alloc_pixels_in(bytes, c->image_in_psram), bytes,
            {c->width, c->height, c->width * bpp, c->format, c->panel_order}
        };
        b->slots[i].draw_owned = !c->external_buffers;
        if (!b->slots[i].surface.pixels) {
            free_backend(b);
            return ESP_ERR_NO_MEM;
        }
        if (physical_partial) {
            b->slots[i].surface.layout.width = quarter ? c->height : c->width;
            b->slots[i].surface.layout.height = quarter ? c->width : c->height;
            b->slots[i].surface.layout.stride_bytes = b->slots[i].surface.layout.width * bpp;
        } else if (c->rotation != PV2_ROTATE_0 || c->draw_order != c->panel_order) {
            bool quarter = c->rotation == PV2_ROTATE_90 || c->rotation == PV2_ROTATE_270;
            b->slots[i].scan = b->slots[i].surface;
            b->slots[i].scan_owned = b->slots[i].draw_owned;
            b->slots[i].scan.layout.width = quarter ? c->height : c->width;
            b->slots[i].scan.layout.height = quarter ? c->width : c->height;
            b->slots[i].scan.layout.stride_bytes = b->slots[i].scan.layout.width * bpp;
            b->slots[i].surface.layout.byte_order = c->draw_order;
            b->slots[i].surface.pixels = i ? b->slots[0].surface.pixels : pv2_alloc_pixels_in(bytes, c->logical_in_psram);
            b->slots[i].draw_owned = i == 0;
            if (!b->slots[i].surface.pixels) {
                free_backend(b);
                return ESP_ERR_NO_MEM;
            }
            b->scan_storage = true;
        }
    }
    if (c->initial_scanout_buffer) {
        if (!c->external_buffers) {
            free_backend(b);
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < c->buffer_count; ++i) {
            if (c->external_buffers[i] == c->initial_scanout_buffer) {
                b->slots[i].state = FB_SLOT_SCANNING;
                b->scanning = i;
                pv2_allocation_hold(&b->image.allocations[i], PV2_REF_SCANOUT);
            }
        }
        if (b->scanning == SIZE_MAX) {
            free_backend(b);
            return ESP_ERR_INVALID_ARG;
        }
    }
    pv2_pixel_config_t pc = {.max_jobs = b->tile_count ? b->tile_count : 1, .asynchronous = c->async_copy,
                             .execute = c->copy_execute, .execute_user = c->copy_user,
                             .on_event = c->async_copy ? copy_done : NULL, .user = b
                            };
    esp_err_t error = pv2_pixel_create(&pc, &b->pixel);
    if (error == ESP_OK) {
        error = c->panel.attach(c->panel.context, dispatch, b);
    }
    if (error != ESP_OK) {
        free_backend(b);
        return error;
    }
    *out = b;
    return ESP_OK;
}

static esp_err_t fb_get_caps(pv2_backend_t *b, pv2_backend_caps_t *out)
{
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_backend_caps_t) {
        .contract = b->config.render_contract,
        .draw_layout = b->slots[0].surface.layout, .drawbuf_capacity_bytes = b->tile_bytes,
        .buffer_alignment = PV2_BUFFER_ALIGNMENT, .max_damage_areas = PV2_MAX_DAMAGE_AREAS,
        .max_tile_rows = b->config.render_contract == PV2_CONTRACT_PARTITION ? b->config.tile_rows : b->config.height,
        .retains_content = true, .supports_present_confirmation = true,
        .writes_active_scanout = b->config.buffer_count == 1
    };
    out->draw_layout.width = b->config.width;
    out->draw_layout.height = b->config.height;
    out->draw_layout.stride_bytes = b->config.width * (b->config.format == PV2_FORMAT_RGB565 ? 2 : 3);
    out->draw_layout.byte_order = b->config.draw_order;
    return ESP_OK;
}

static esp_err_t validate_damage(pv2_backend_t *b, const pv2_rect_t *areas, size_t count)
{
    return pv2_validate_damage(b->config.width, b->config.height, areas, count);
}

static bool post_seal_stage(fb_state_t state)
{
    return state == FB_SLOT_DRAIN_TILE || state == FB_SLOT_REPAIR || state == FB_SLOT_CONVERT;
}

static esp_err_t fb_begin(pv2_backend_t *b, const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!b || !request) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = validate_damage(b, request->damage, request->damage_count);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_fb_service(b);
    if (b->active != SIZE_MAX) {
        return post_seal_stage(b->slots[b->active].state) ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    }
    error = pv2_frame_core_admit(&b->core, &b->lock, b->config.width, b->config.height, request, out);
    if (error != ESP_OK) {
        return error;
    }
    size_t slot = SIZE_MAX, record = SIZE_MAX;
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        if (b->slots[i].state == FB_SLOT_FREE || (b->config.buffer_count == 1 && b->slots[i].state == FB_SLOT_SCANNING)) {
            slot = i;
            break;
        }
    }
    for (size_t i = 0; i < b->config.max_frames; i++) {
        if (!b->core.frames[i].common.token) {
            record = i;
            break;
        }
    }
    if (slot == SIZE_MAX || record == SIZE_MAX) {
        return ESP_ERR_NO_MEM;
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    /* Prepopulate a stable latest sealed frame. Sealed buffers are immutable;
     * single-producer pump cannot retire/reuse it during this CPU copy. */
    bool prefill = b->baseline != SIZE_MAX && b->baseline != slot && b->config.render_contract == PV2_CONTRACT_DIRECT;
    error = pv2_image_begin(&b->image, slot, b->baseline);
    if (error != ESP_OK) {
        return error;
    }
    if (prefill && !b->config.async_copy) {
        pv2_copy_request_t copy = baseline_copy(b, slot, (pv2_rect_t) {
            0, 0, b->config.width, b->config.height
        });
        error = pv2_pixel_execute_cpu(&copy);
        if (error != ESP_OK) {
            pv2_image_cancel(&b->image);
            return error;
        }
    }
    pv2_lock_enter(&b->lock, false);
    b->active = slot;
    b->slots[slot].state = (prefill && b->config.async_copy) ? FB_SLOT_PREFILL : FB_SLOT_DRAWING;
    b->slots[slot].record = record;
    b->core.frames[record] = (fb_record_t) {
        .common = {.token = token}
    };
    pv2_lock_exit(&b->lock, false);
    b->submitted = false;
    b->repair_pos = 0;
    b->converted = false;
    if (prefill && b->config.async_copy) {
        pv2_copy_request_t copy = baseline_copy(b, slot, (pv2_rect_t) {
            0, 0, b->config.width, b->config.height
        });
        error = pv2_pixel_submit(b->pixel, &copy, &b->copy);
        if (error != ESP_OK) {
            pv2_lock_enter(&b->lock, false);
            pv2_image_cancel(&b->image);
            b->core.frames[record].common.token = 0;
            b->slots[slot].state = FB_SLOT_FREE;
            b->active = SIZE_MAX;
            pv2_lock_exit(&b->lock, false);
            return error;
        }
        b->copy_is_tile = false;
    }
    out->value = token;
    return ESP_OK;
}

static bool building(pv2_backend_t *b, pv2_frame_t frame)
{
    if (!b || (b->core.life != PV2_CORE_RUNNING) || b->active == SIZE_MAX || !frame.value) {
        return false;
    }
    fb_state_t st = b->slots[b->active].state;
    return (st == FB_SLOT_PREFILL || st == FB_SLOT_DRAWING) &&
           b->core.frames[b->slots[b->active].record].common.token == frame.value;
}

static esp_err_t fb_acquire(pv2_backend_t *b, pv2_frame_t frame, const pv2_rect_t *area,
                            uint32_t timeout, pv2_lease_t *out)
{
    (void)timeout;
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_lease_t) {
        0
    };
    if (!building(b, frame)) {
        return ESP_ERR_INVALID_STATE;
    }
    progress_copy(b);
    pv2_lock_enter(&b->lock, false);
    bool failed = b->core.fault.result != ESP_OK;
    pv2_lock_exit(&b->lock, false);
    if (failed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (b->slots[b->active].state == FB_SLOT_PREFILL || b->lease.id.value || b->copy.value) {
        return ESP_ERR_TIMEOUT;
    }
    fb_tile_t *tile = NULL;
    if (b->tile_count) {
        for (size_t i = 0; i < b->tile_count; i++)
            if (!b->tiles[i].lease.id.value && !b->tiles[i].copy.value) {
                tile = &b->tiles[i];
                break;
            }
        if (!tile) {
            return ESP_ERR_TIMEOUT;
        }
    }
    pv2_rect_t actual = {0, 0, b->config.width, b->config.height};
    pv2_surface_t surface = b->slots[b->active].surface;
    if (area && !valid_area(b, area)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (b->config.render_contract == PV2_CONTRACT_PARTITION) {
        actual = area ? *area : (pv2_rect_t) {
            0, 0, b->config.width, b->config.tile_rows
        };
        if (actual.height > b->config.tile_rows) {
            actual.height = b->config.tile_rows;
        }
        size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
        surface = (pv2_surface_t) {
            tile->pixels, b->tile_bytes,
            {actual.width, actual.height, actual.width * bpp, b->config.format, b->config.draw_order}
        };
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    pv2_lease_t *lease = tile ? &tile->lease : &b->lease;
    *lease = (pv2_lease_t) {
        .id = {token}, .surface = surface, .logical_area = actual
    };
    *out = *lease;
    return ESP_OK;
}

static esp_err_t fb_submit_region_ex(pv2_backend_t *b, pv2_frame_t frame,
                                     pv2_lease_id_t lease, const pv2_region_submit_t *submit)
{
    fb_tile_t *tile = NULL;
    pv2_lease_t *draw = b ? find_draw_lease(b, lease.value, &tile) : NULL;
    if (!building(b, frame) || !draw) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_lock_enter(&b->lock, false);
    bool failed = b->core.fault.result != ESP_OK;
    pv2_lock_exit(&b->lock, false);
    if (failed) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_rect_t area = draw->logical_area;
    if (submit && (submit->logical_area.x || submit->logical_area.y || submit->logical_area.width || submit->logical_area.height)) {
        area = submit->logical_area;
    }
    if (!valid_area(b, &area)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (b->config.render_contract == PV2_CONTRACT_PARTITION) {
        size_t stride = submit && submit->stride_bytes ? submit->stride_bytes : area.width * bpp;
        pv2_rect_t physical;
        esp_err_t map_error = pv2_rotate_rect(b->config.width, b->config.height, b->config.rotation, &area, &physical);
        if (map_error != ESP_OK) {
            return map_error;
        }
        pv2_copy_request_t copy = {
            .source = {tile->pixels, b->tile_bytes, {area.width, area.height, stride, b->config.format, b->config.draw_order}},
            .source_area = {0, 0, area.width, area.height}, .destination = b->slots[b->active].surface,
            .destination_x = physical.x, .destination_y = physical.y, .rotation = b->config.rotation
        };
        pv2_copy_ticket_t ticket;
        esp_err_t error = pv2_pixel_submit(b->pixel, &copy, &ticket);
        if (error != ESP_OK) {
            return error;
        }
        if (b->config.async_copy) {
            tile->copy = ticket;
            tile->area = physical;
            draw->id.value = 0;
            return ESP_OK;
        }
        pv2_copy_status_t status;
        error = pv2_pixel_wait(b->pixel, ticket, UINT32_MAX, &status);
        (void)pv2_pixel_release(b->pixel, ticket);
        if (error != ESP_OK || status.result != ESP_OK) {
            return error != ESP_OK ? error : status.result;
        }
    } else {
        if (submit && submit->stride_bytes && submit->stride_bytes != draw->surface.layout.stride_bytes) {
            return ESP_ERR_INVALID_ARG;
        }
        if (b->config.render_contract == PV2_CONTRACT_FULL &&
                (area.x || area.y || area.width != b->config.width || area.height != b->config.height)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (b->config.render_contract == PV2_CONTRACT_PARTITION) {
        pv2_rect_t physical;
        (void)pv2_rotate_rect(b->config.width, b->config.height, b->config.rotation, &area, &physical);
        cover(b, physical);
    } else {
        cover(b, area);
    }
    b->submitted = true;
    draw->id.value = 0;
    return ESP_OK;
}

static esp_err_t fb_submit_region(pv2_backend_t *b, pv2_frame_t frame, pv2_lease_id_t lease)
{
    return fb_submit_region_ex(b, frame, lease, NULL);
}

static esp_err_t fb_cancel_region(pv2_backend_t *b, pv2_frame_t frame, pv2_lease_id_t lease)
{
    fb_tile_t *tile = NULL;
    pv2_lease_t *draw = b ? find_draw_lease(b, lease.value, &tile) : NULL;
    if (!building(b, frame) || !draw) {
        return ESP_ERR_INVALID_STATE;
    }
    draw->id.value = 0;
    return ESP_OK;
}

static esp_err_t fb_get_draw_buffers(pv2_backend_t *b, void **out, size_t capacity, size_t *count, size_t *bytes)
{
    if (!b || !count || (capacity && !out)) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = b->tile_count ? b->tile_count : b->scan_storage ? 1 : b->config.buffer_count;
    if (bytes) {
        *bytes = b->tile_bytes;
    }
    if (capacity < *count) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < *count; i++) {
        out[i] = b->tile_count ? b->tiles[i].pixels : b->slots[i].surface.pixels;
    }
    return ESP_OK;
}

static esp_err_t fb_end_ex(pv2_backend_t *b, pv2_frame_t frame,
                           const pv2_frame_coverage_t *coverage, pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (!building(b, frame) || b->lease.id.value || tiles_leased(b)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (coverage) {
        esp_err_t error = validate_damage(b, coverage->areas, coverage->area_count);
        if (error != ESP_OK) {
            return error;
        }
    }
    fb_slot_t *slot = &b->slots[b->active];
    fb_record_t *record = &b->core.frames[slot->record];
    slot->state = FB_SLOT_DRAIN_TILE;
    record->common.external = out != NULL;
    record->sealed = true;
    record->pending = 1;
    fb_advance_result_t progress = advance_build_pipeline(b, FB_SEAL_ATTEMPT);
    if (progress.state == FB_ADVANCE_PENDING) {
        if (out) {
            out->value = frame.value;
        }
        return ESP_OK;
    }
    if (progress.state != FB_ADVANCE_COMPLETE) {
        slot->state = FB_SLOT_DRAWING;
        record->common.external = false;
        record->sealed = false;
        record->pending = 0;
        return progress.state == FB_ADVANCE_FAILED ? progress.error : ESP_ERR_INVALID_STATE;
    }
    if (out) {
        out->value = frame.value;
    }
    return ESP_OK;
}

static esp_err_t fb_end(pv2_backend_t *b, pv2_frame_t frame, pv2_receipt_t *out)
{
    return fb_end_ex(b, frame, NULL, out);
}

static esp_err_t fb_cancel_frame(pv2_backend_t *b, pv2_frame_t frame, uint32_t timeout)
{
    if (!b || !frame.value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pv2_cancel_matches(&b->core.canceled, frame.value)) {
        return ESP_OK;
    }
    if (b->active != SIZE_MAX && b->slots[b->active].state == FB_SLOT_CANCELING &&
            b->core.frames[b->slots[b->active].record].common.token == frame.value) {
        /* Retry the same cancellation, without reopening drawing. */
    } else if (!building(b, frame)) {
        return ESP_ERR_INVALID_STATE;
    }
    b->slots[b->active].state = FB_SLOT_CANCELING;
    b->lease.id.value = 0;
    for (size_t i = 0; i < b->tile_count; i++) {
        b->tiles[i].lease.id.value = 0;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        (void)advance_build_pipeline(b, FB_SEAL_ACCEPTED);
        if (pv2_cancel_matches(&b->core.canceled, frame.value)) {
            return ESP_OK;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

static esp_err_t fb_query(pv2_backend_t *b, pv2_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_fb_service(b);
    return pv2_frame_core_query(&b->core, &b->lock, receipt, true, out);
}

static esp_err_t fb_get_fault(pv2_backend_t *b, pv2_backend_fault_t *out)
{
    return b ? pv2_frame_core_get_fault(&b->core, &b->lock, out) : ESP_ERR_INVALID_ARG;
}

static esp_err_t fb_wait(pv2_backend_t *b, pv2_receipt_t receipt, pv2_completion_point_t point,
                         uint32_t timeout, pv2_frame_status_t *out)
{
    return pv2_frame_core_wait(b, fb_query, receipt, point, true, timeout, out);
}

static esp_err_t fb_release_receipt(pv2_backend_t *b, pv2_receipt_t receipt)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_fb_service(b);
    return pv2_frame_core_release_receipt(&b->core, &b->lock, receipt);
}

static esp_err_t fb_stop(pv2_backend_t *b, uint32_t timeout)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((b->core.life == PV2_CORE_QUIESCENT)) {
        return ESP_OK;
    }
    b->core.life = PV2_CORE_CLOSING;
    uint64_t start = pv2_now_ms();
    esp_err_t error = pv2_pixel_stop(b->pixel, timeout);
    if (error != ESP_OK) {
        return error;
    }
    progress_copy(b);
    uint32_t left = pv2_remaining_ms(start, timeout);
    error = b->config.panel.quiesce(b->config.panel.context, left);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_fb_service(b);
    pv2_lock_enter(&b->lock, false);
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        fb_slot_t *slot = &b->slots[i];
        if (slot->state != FB_SLOT_FREE && slot->state != FB_SLOT_SCANNING) {
            fault(b, i, ESP_ERR_INVALID_STATE);
            terminal(b, i);
        }
        slot->state = FB_SLOT_FREE;
        b->image.allocations[i] = (pv2_allocation_t) {
            0
        };
    }
    b->image.published_baseline = SIZE_MAX;
    b->active = b->pending = b->scanning = b->baseline = SIZE_MAX;
    b->lease.id.value = 0;
    for (size_t i = 0; i < b->tile_count; i++) {
        b->tiles[i].lease.id.value = 0;
    }
    b->core.life = PV2_CORE_QUIESCENT;
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static esp_err_t fb_destroy(pv2_backend_t *b)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = pv2_frame_core_can_destroy(&b->core);
    if (err != ESP_OK) {
        return err;
    }
    free_backend(b);
    return ESP_OK;
}

static esp_err_t fb_set_rotation(pv2_backend_t *b, pv2_rotation_t rotation)
{
    if (!b || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((b->core.life != PV2_CORE_RUNNING) || b->active != SIZE_MAX || b->pending != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < b->config.max_frames; ++i) {
        if (b->core.frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (rotation == b->config.rotation) {
        return ESP_OK;
    }
    bool axes = ((unsigned)rotation ^ (unsigned)b->config.rotation) & 1;
    uint32_t width = axes ? b->config.height : b->config.width;
    uint32_t height = axes ? b->config.width : b->config.height;
    size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t rows = b->tile_bytes / ((size_t)width * bpp);
    if (!rows) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (b->config.render_contract == PV2_CONTRACT_PARTITION) {
        b->config.width = width;
        b->config.height = height;
        b->config.rotation = rotation;
        b->config.tile_rows = rows > height ? height : rows;
        b->baseline = SIZE_MAX;
        pv2_image_reset(&b->image, b->image.width, b->image.height);
        return ESP_OK;
    }
    bool need_scan_storage = rotation != PV2_ROTATE_0 || b->config.draw_order != b->config.panel_order;
    if (!need_scan_storage && b->scan_storage) {
        /* Drained receipts do not retire current scanout. Preserve the physical
         * pool and each slot's FB_SLOT_SCANNING/FB_SLOT_FREE state; free only logical storage. */
        for (size_t i = 0; i < b->config.buffer_count; i++) {
            if (b->slots[i].draw_owned) {
                pv2_free(b->slots[i].surface.pixels);
            }
            b->slots[i].surface = b->slots[i].scan;
            b->slots[i].draw_owned = b->slots[i].scan_owned;
            b->slots[i].scan = (pv2_surface_t) {
                0
            };
            b->slots[i].scan_owned = false;
        }
        b->scan_storage = false;
    } else if (need_scan_storage && !b->scan_storage) {
        void *draw = pv2_alloc_pixels_in(b->bytes, b->config.logical_in_psram);
        if (!draw) {
            return ESP_ERR_NO_MEM;
        }
        /* Preserve every existing physical scan pointer, including current. */
        for (size_t i = 0; i < b->config.buffer_count; ++i) {
            b->slots[i].scan = b->slots[i].surface;
            b->slots[i].scan_owned = b->slots[i].draw_owned;
            b->slots[i].surface.pixels = draw;
            b->slots[i].surface.layout.byte_order = b->config.draw_order;
            b->slots[i].draw_owned = i == 0;
        }
        b->scan_storage = true;
    }
    for (size_t i = 0; i < b->config.buffer_count; ++i) {
        b->slots[i].surface.layout.width = width;
        b->slots[i].surface.layout.height = height;
        b->slots[i].surface.layout.stride_bytes = width * bpp;
        b->slots[i].surface.layout.byte_order = b->config.draw_order;
    }
    b->config.width = width;
    b->config.height = height;
    b->config.rotation = rotation;
    b->config.tile_rows = rows > height ? height : rows;
    b->baseline = SIZE_MAX; /* Logical orientation changed: require full redraw. */
    pv2_image_reset(&b->image, width, height);
    return ESP_OK;
}

const pv2_backend_ops_t pv2_fb_backend = {
    .name = "fb",
    .create = fb_create,
    .get_caps = fb_get_caps,
    .begin = fb_begin,
    .acquire = fb_acquire,
    .get_draw_buffers = fb_get_draw_buffers,
    .submit_region = fb_submit_region,
    .submit_region_ex = fb_submit_region_ex,
    .cancel_region = fb_cancel_region,
    .end = fb_end,
    .end_ex = fb_end_ex,
    .cancel_frame = fb_cancel_frame,
    .query = fb_query,
    .get_fault = fb_get_fault,
    .wait = fb_wait,
    .release_receipt = fb_release_receipt,
    .on_event_isr = fb_on_event_isr,
    .on_event = fb_on_event,
    .stop = fb_stop,
    .destroy = fb_destroy,
    .set_rotation = fb_set_rotation,
};

size_t pv2_fb_memory(const pv2_backend_config_t *c)
{
    size_t tiles = c->render_contract == PV2_CONTRACT_PARTITION ? (c->tile_buffer_count ? c->tile_buffer_count : 1) : 0;
    return sizeof(pv2_backend_t) + tiles * sizeof(fb_tile_t) + c->max_frames * sizeof(pv2_core_frame_t) +
           pv2_pixel_memory(tiles ? tiles : 1, c->async_copy);
}
