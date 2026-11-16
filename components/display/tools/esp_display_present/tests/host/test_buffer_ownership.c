/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "backend/present_backend.h"
#include "backend/present_backend_config.h"
#include "pixel/present_pixel_ops.h"
#include "platform/present_os.h"
#include <assert.h>
#include <string.h>

#define OK(call) assert((call) == ESP_OK)

enum { WIDTH = 4, HEIGHT = 4, TILE_ROWS = 2, STRIDE = WIDTH * 2 };

static const esp_display_present_frame_request_t redraw = {.full_redraw = true};
static const esp_display_present_rect_t top = {0, 0, WIDTH, TILE_ROWS};
static const esp_display_present_rect_t bottom = {0, TILE_ROWS, WIDTH, TILE_ROWS};
static const esp_display_present_rect_t screen = {0, 0, WIDTH, HEIGHT};

typedef struct {
    const present_backend_ops_t *ops;
    present_backend_t *backend;
    esp_display_present_panel_event_fn callback;
    void *callback_user;
    esp_display_present_event_kind_t completion;
    esp_display_present_tx_acceptance_t acceptance;
    bool early, scan_stopped;
    esp_display_present_receipt_t early_receipt;
    unsigned submissions, stop_calls;
    uint64_t cookie, edge;
    const void *offered;
    const void *pending;
    const void *scanning;
    present_os_event_t *copy_started, *copy_resume, *copy_done;
    void *copy_destination;
} fixture_t;

static esp_err_t attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    fixture_t *f = context;
    f->callback = callback;
    f->callback_user = user;
    return ESP_OK;
}

static void emit(fixture_t *f, esp_display_present_event_kind_t kind)
{
    assert(f->callback && f->cookie);
    const esp_display_present_backend_event_t event = {
        .kind = kind, .transaction_cookie = f->cookie,
    };
    (void)f->callback(f->callback_user, &event, true);
}

static void complete(fixture_t *f)
{
    if (f->completion == ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED) {
        f->scanning = f->pending;
    }
    emit(f, f->completion);
    f->cookie = 0;
    f->pending = NULL;
}

static esp_display_present_tx_result_t submit(void *context, const esp_display_present_surface_t *source,
                                              const esp_display_present_rect_t *area, uint64_t cookie)
{
    (void)area;
    fixture_t *f = context;
    assert(!f->cookie);
    f->offered = source->pixels;
    ++f->submissions;
    if (f->acceptance != ESP_DISPLAY_PRESENT_TX_REJECTED) {
        f->pending = source->pixels;
        f->cookie = cookie;
        if (f->early) {
            complete(f);
            if (f->early_receipt.value) {
                esp_display_present_frame_status_t status;
                OK(f->ops->peek(f->backend, f->early_receipt, &status));
                assert(!status.terminal && !status.src_released);
            }
        }
    }
    return (esp_display_present_tx_result_t) {
        f->acceptance, f->acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED ? ESP_OK : ESP_FAIL
    };
}

static esp_err_t quiesce(void *context, uint32_t timeout)
{
    (void)timeout;
    fixture_t *f = context;
    ++f->stop_calls;
    if (f->pending || (f->scanning && !f->scan_stopped)) {
        return ESP_ERR_TIMEOUT;
    }
    f->scanning = NULL;
    f->callback = NULL;
    return ESP_OK;
}

static esp_err_t admit_te(void *context, size_t bytes, uint64_t after, uint64_t *edge)
{
    fixture_t *f = context;
    assert(bytes == STRIDE * HEIGHT);
    *edge = ++f->edge;
    assert(*edge > after);
    return ESP_OK;
}

static esp_err_t gated_copy(void *context, const esp_display_present_copy_request_t *request)
{
    fixture_t *f = context;
    f->copy_destination = request->dst.pixels;
    (void)present_event_signal(f->copy_started, false);
    assert(present_event_wait(f->copy_resume, 1000));
    return esp_display_present_pixel_execute_cpu(request);
}

static bool copy_progress(void *context, bool isr)
{
    fixture_t *f = context;
    return present_event_signal(f->copy_done, isr);
}

static void open_backend(fixture_t *f, bool staging)
{
    bool fb = f->ops == &present_fb_backend;
    bool te = f->ops == &present_te_backend;
    f->completion = fb ? ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED : ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE;
    esp_display_present_backend_config_t config = {
        .width = WIDTH, .height = HEIGHT, .tile_rows = TILE_ROWS,
        .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .buffer_count = fb ? 2 : 1, .max_frames = 4,
        .render_contract = fb ? ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL : ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
        .panel = {f, attach, submit, quiesce},
        .tx_staging = staging,
    };
    if (te) {
        config.compose_count = 1;
        config.te_gate = (esp_display_present_te_gate_t) {
            .admit = admit_te, .user = f
        };
    }
    if (te || staging) {
        f->copy_started = present_event_create();
        f->copy_resume = present_event_create();
        f->copy_done = present_event_create();
        assert(f->copy_started && f->copy_resume && f->copy_done);
        config.async_copy = true;
        config.copy_execute = gated_copy;
        config.copy_user = f;
        config.on_progress = copy_progress;
        config.progress_user = f;
    }
    OK(f->ops->create(&config, &f->backend));
}

static esp_display_present_frame_t begin(fixture_t *f)
{
    esp_display_present_frame_t frame;
    OK(f->ops->begin(f->backend, &redraw, &frame));
    return frame;
}

static void expect_no_frame(fixture_t *f, esp_err_t error)
{
    esp_display_present_frame_t frame = {0};
    assert(f->ops->begin(f->backend, &redraw, &frame) == error && !frame.value);
}

static esp_display_present_lease_t lease(fixture_t *f, esp_display_present_frame_t frame, const esp_display_present_rect_t *area)
{
    esp_display_present_lease_t out;
    OK(f->ops->acquire(f->backend, frame, area, 0, &out));
    assert(out.id.value && out.surface.pixels);
    return out;
}

static void expect_no_lease(fixture_t *f, esp_display_present_frame_t frame, esp_err_t error)
{
    esp_display_present_lease_t unavailable = {0};
    assert(f->ops->acquire(f->backend, frame, NULL, 0, &unavailable) == error);
    assert(!unavailable.id.value && !unavailable.surface.pixels);
}

static void expect_released(esp_display_present_lease_t previous, esp_display_present_lease_t next)
{
    assert(next.surface.pixels == previous.surface.pixels && next.id.value != previous.id.value);
}

static void draw(esp_display_present_lease_t buffer, uint8_t value)
{
    memset(buffer.surface.pixels, value, buffer.surface.capacity_bytes);
}

static void expect_bytes(const void *buffer, size_t size, uint8_t value)
{
    assert(buffer);
    for (size_t i = 0; i < size; ++i) {
        assert(((const uint8_t *)buffer)[i] == value);
    }
}

static esp_display_present_lease_t start_tile_copy(fixture_t *f, esp_display_present_frame_t frame,
                                                   const esp_display_present_rect_t *area, uint8_t value)
{
    esp_display_present_lease_t tile = lease(f, frame, area);
    draw(tile, value);
    OK(f->ops->submit_region(f->backend, frame, tile.id));
    assert(present_event_wait(f->copy_started, 1000));
    return tile;
}

static void finish_copy(fixture_t *f)
{
    (void)present_event_signal(f->copy_resume, false);
    assert(present_event_wait(f->copy_done, 1000));
    OK(f->ops->service(f->backend));
}

static esp_display_present_receipt_t seal(fixture_t *f, esp_display_present_frame_t frame)
{
    esp_display_present_receipt_t receipt;
    OK(f->ops->end(f->backend, frame, &receipt));
    return receipt;
}

static void release_receipt(fixture_t *f, esp_display_present_receipt_t receipt, esp_err_t result)
{
    esp_display_present_frame_status_t status;
    OK(f->ops->query(f->backend, receipt, &status));
    assert(status.terminal && status.src_released && status.result == result);
    OK(f->ops->release_receipt(f->backend, receipt));
}

static void stop_with_pending(fixture_t *f, esp_display_present_frame_t frame,
                              esp_display_present_receipt_t receipt, size_t bytes, uint8_t value)
{
    const void *source = f->pending;
    uint64_t cookie = f->cookie;
    unsigned submissions = f->submissions;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        unsigned stops = f->stop_calls;
        assert(f->ops->stop(f->backend, 1000) == ESP_ERR_TIMEOUT);
        assert(f->stop_calls == stops + 1);
        assert(f->ops->destroy(f->backend) == ESP_ERR_INVALID_STATE);
        expect_no_frame(f, ESP_ERR_INVALID_STATE);
        expect_no_lease(f, frame, ESP_ERR_INVALID_STATE);
        esp_display_present_frame_status_t status;
        OK(f->ops->peek(f->backend, receipt, &status));
        assert(!status.terminal && !status.src_released && status.result == ESP_FAIL);
        assert(f->pending == source && f->cookie == cookie && f->callback && f->submissions == submissions);
        expect_bytes(source, bytes, value);
    }
}

static void close_backend(fixture_t *f)
{
    OK(f->ops->stop(f->backend, 1000));
    OK(f->ops->destroy(f->backend));
    if (f->copy_started) {
        present_event_destroy(f->copy_started);
        present_event_destroy(f->copy_resume);
        present_event_destroy(f->copy_done);
    }
}

static void region_direct(void)
{
    fixture_t f = {.ops = &present_region_backend, .acceptance = ESP_DISPLAY_PRESENT_TX_REJECTED};
    open_backend(&f, false);
    esp_display_present_frame_t frame = begin(&f);
    esp_display_present_lease_t first = lease(&f, frame, &top);
    draw(first, 0x11);
    assert(f.ops->submit_region(f.backend, frame, first.id) == ESP_FAIL);
    assert(f.offered == first.surface.pixels && !f.pending);
    expect_no_lease(&f, frame, ESP_ERR_TIMEOUT); /* The producer still owns its original lease. */
    draw(first, 0x22);
    f.acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED;
    f.early = true;
    OK(f.ops->submit_region(f.backend, frame, first.id));

    esp_display_present_lease_t second = lease(&f, frame, &bottom);
    expect_released(first, second);
    assert(f.ops->submit_region(f.backend, frame, first.id) == ESP_ERR_INVALID_STATE);
    draw(second, 0x33);
    f.early = false;
    OK(f.ops->submit_region(f.backend, frame, second.id));
    esp_display_present_receipt_t previous = seal(&f, frame);
    frame = begin(&f);
    expect_no_lease(&f, frame, ESP_ERR_TIMEOUT);
    assert(f.pending == second.surface.pixels);
    expect_bytes(f.pending, STRIDE * TILE_ROWS, 0x33);
    complete(&f);
    esp_display_present_lease_t third = lease(&f, frame, &top);
    expect_released(second, third); /* Keeping the previous receipt does not keep the write lease. */
    release_receipt(&f, previous, ESP_OK);
    draw(third, 0x44);
    f.acceptance = ESP_DISPLAY_PRESENT_TX_UNCERTAIN;
    OK(f.ops->submit_region(f.backend, frame, third.id));
    esp_display_present_receipt_t pending = seal(&f, frame);
    stop_with_pending(&f, frame, pending, STRIDE * TILE_ROWS, 0x44);
    complete(&f);
    expect_no_frame(&f, ESP_ERR_INVALID_STATE);
    release_receipt(&f, pending, ESP_FAIL);
    close_backend(&f);
}

static void region_staged(void)
{
    fixture_t f = {.ops = &present_region_backend, .acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED};
    open_backend(&f, true);
    esp_display_present_frame_t frame = begin(&f);
    esp_display_present_lease_t first = start_tile_copy(&f, frame, &top, 0x55);
    expect_no_lease(&f, frame, ESP_ERR_TIMEOUT);
    assert(!f.pending);
    finish_copy(&f);
    esp_display_present_lease_t second = lease(&f, frame, &bottom);
    expect_released(first, second);
    assert(f.pending == f.copy_destination && f.pending != second.surface.pixels);
    draw(second, 0x66);
    assert(f.ops->submit_region(f.backend, frame, second.id) == ESP_ERR_NO_MEM);
    expect_bytes(f.pending, STRIDE * TILE_ROWS, 0x55);
    complete(&f);
    OK(f.ops->cancel_region(f.backend, frame, second.id));
    release_receipt(&f, seal(&f, frame), ESP_OK);
    close_backend(&f);
}

static void framebuffer(void)
{
    fixture_t f = {.ops = &present_fb_backend, .acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED, .early = true};
    open_backend(&f, false);
    esp_display_present_frame_t frame = begin(&f);
    esp_display_present_lease_t first = lease(&f, frame, &screen);
    draw(first, 0x11);
    OK(f.ops->submit_region(f.backend, frame, first.id));
    esp_display_present_receipt_t first_receipt = seal(&f, frame);
    f.early_receipt = first_receipt;
    OK(f.ops->service(f.backend));
    release_receipt(&f, first_receipt, ESP_OK);
    assert(f.scanning == first.surface.pixels);

    frame = begin(&f);
    esp_display_present_lease_t second = lease(&f, frame, &screen);
    assert(second.surface.pixels != first.surface.pixels);
    draw(second, 0x22);
    OK(f.ops->submit_region(f.backend, frame, second.id));
    esp_display_present_receipt_t second_receipt = seal(&f, frame);
    f.acceptance = ESP_DISPLAY_PRESENT_TX_REJECTED;
    f.early = false;
    assert(f.ops->service(f.backend) == ESP_FAIL && f.offered == second.surface.pixels);
    expect_no_frame(&f, ESP_ERR_NO_MEM); /* One slot scans, the rejected candidate stays queued. */
    expect_bytes(f.scanning, STRIDE * HEIGHT, 0x11);

    f.acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED;
    OK(f.ops->service(f.backend));
    assert(f.pending == second.surface.pixels);
    emit(&f, ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE);
    expect_no_frame(&f, ESP_ERR_NO_MEM);
    complete(&f);
    frame = begin(&f);
    esp_display_present_lease_t third = lease(&f, frame, &screen);
    expect_released(first, third); /* Only replacement confirmation frees the old scanout. */
    assert(f.scanning == second.surface.pixels);
    release_receipt(&f, second_receipt, ESP_OK);

    draw(third, 0x33);
    OK(f.ops->submit_region(f.backend, frame, third.id));
    esp_display_present_receipt_t pending = seal(&f, frame);
    f.acceptance = ESP_DISPLAY_PRESENT_TX_UNCERTAIN;
    OK(f.ops->service(f.backend));
    stop_with_pending(&f, frame, pending, STRIDE * HEIGHT, 0x33);
    expect_bytes(f.scanning, STRIDE * HEIGHT, 0x22);
    complete(&f);
    expect_no_frame(&f, ESP_ERR_INVALID_STATE);
    release_receipt(&f, pending, ESP_FAIL);
    assert(f.ops->stop(f.backend, 1000) == ESP_ERR_TIMEOUT);
    assert(f.scanning == third.surface.pixels); /* Confirmed FB is still read by scanout. */
    expect_bytes(f.scanning, STRIDE * HEIGHT, 0x33);
    f.scan_stopped = true;
    close_backend(&f);
}

static void framebuffer_shared_worker_tile(void)
{
    fixture_t f = {
        .ops = &present_fb_backend,
        .acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED,
        .completion = ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED,
    };
    esp_display_present_backend_config_t config = {
        .width = WIDTH, .height = HEIGHT, .tile_rows = TILE_ROWS,
        .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .buffer_count = 2, .max_frames = 4,
        .render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
        .tile_count = 1, .async_copy = true, .shared_copy_worker = true,
        .panel = {&f, attach, submit, quiesce},
    };
    OK(f.ops->create(&config, &f.backend));
    esp_display_present_frame_t frame = begin(&f);
    esp_display_present_lease_t first = lease(&f, frame, &top);
    draw(first, 0x55);
    OK(f.ops->submit_region(f.backend, frame, first.id));
    expect_no_lease(&f, frame, ESP_ERR_TIMEOUT);
    assert(f.ops->copy_work(f.backend));
    esp_display_present_lease_t second = lease(&f, frame, &bottom);
    expect_released(first, second);
    draw(second, 0x55);
    OK(f.ops->submit_region(f.backend, frame, second.id));
    assert(f.ops->copy_work(f.backend));
    esp_display_present_receipt_t receipt = seal(&f, frame);
    OK(f.ops->service(f.backend));
    assert(f.pending && f.pending != first.surface.pixels);
    expect_bytes(f.pending, STRIDE * HEIGHT, 0x55);
    const void *pending_image = f.pending;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        assert(f.ops->stop(f.backend, 1) == ESP_ERR_TIMEOUT);
        assert(f.ops->destroy(f.backend) == ESP_ERR_INVALID_STATE);
        expect_no_frame(&f, ESP_ERR_INVALID_STATE);
        esp_display_present_frame_status_t status;
        OK(f.ops->peek(f.backend, receipt, &status));
        assert(!status.terminal && !status.src_released);
        assert(f.pending == pending_image);
        expect_bytes(pending_image, STRIDE * HEIGHT, 0x55);
    }
    complete(&f);
    release_receipt(&f, receipt, ESP_OK);
    f.scan_stopped = true;
    close_backend(&f);
}

static void te(void)
{
    fixture_t f = {.ops = &present_te_backend, .acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED};
    open_backend(&f, false);
    esp_display_present_frame_t frame = begin(&f);
    esp_display_present_lease_t tile = start_tile_copy(&f, frame, &top, 0x11);
    const void *image = f.copy_destination;
    assert(image != tile.surface.pixels);
    expect_no_lease(&f, frame, ESP_ERR_TIMEOUT);
    finish_copy(&f);

    esp_display_present_lease_t next = start_tile_copy(&f, frame, &bottom, 0x11);
    expect_released(tile, next); /* Copy done gives back the tile before any panel TX. */
    finish_copy(&f);
    esp_display_present_receipt_t first_receipt = seal(&f, frame);
    f.acceptance = ESP_DISPLAY_PRESENT_TX_REJECTED;
    assert(f.ops->service(f.backend) == ESP_FAIL && f.offered == image && !f.pending);
    expect_no_frame(&f, ESP_ERR_NO_MEM); /* Rejection keeps the only compose image queued. */
    expect_bytes(image, STRIDE * HEIGHT, 0x11);

    f.acceptance = ESP_DISPLAY_PRESENT_TX_ACCEPTED;
    f.early = true;
    f.early_receipt = first_receipt;
    OK(f.ops->service(f.backend));
    assert(f.offered == image && !f.pending);
    frame = begin(&f);
    tile = start_tile_copy(&f, frame, &top, 0x22);
    expect_released(next, tile);
    assert(f.copy_destination == image); /* The next frame can now write the same compose image. */
    finish_copy(&f);
    release_receipt(&f, first_receipt, ESP_OK);
    next = start_tile_copy(&f, frame, &bottom, 0x22);
    expect_released(tile, next);
    finish_copy(&f);
    esp_display_present_receipt_t pending = seal(&f, frame);
    f.early = false;
    f.acceptance = ESP_DISPLAY_PRESENT_TX_UNCERTAIN;
    OK(f.ops->service(f.backend));
    assert(f.pending == image);
    stop_with_pending(&f, frame, pending, STRIDE * HEIGHT, 0x22);
    complete(&f);
    expect_no_frame(&f, ESP_ERR_INVALID_STATE);
    release_receipt(&f, pending, ESP_FAIL);
    close_backend(&f);
}

int main(void)
{
    region_direct();
    region_staged();
    framebuffer();
    framebuffer_shared_worker_tile();
    te();
}
