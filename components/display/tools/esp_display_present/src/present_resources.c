/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_resources.h"
#include "present_os.h"
#include "present_records.h"
#include "present_memory.h"
#include "present_presenter.h"
#include "present_lcd_spi.h"
#include "esp_lcd_io_spi.h"
#include "platform/present_te_sync.h"
#include "present_lcd_scanout.h"
#include <string.h>
#ifndef PRESENT_HOST
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <inttypes.h>
#endif

struct present_resources {
    esp_display_present_engine_t *presenter;
    esp_display_present_lcd_scanout_t *scanout;
    esp_display_present_lcd_spi_t *spi;
    esp_display_present_te_sync_context_t *te;
    esp_display_present_te_sync_config_t te_config;
    bool te_required, te_armed;
    esp_display_present_executor_t *cpu, *dma2d, *ppa;
    esp_display_present_executor_dispatch_t dispatch;
    present_lock_t lock;
    present_resource_status_t status;
    void *buffers[3];
};

static esp_err_t te_admit(void *user, size_t bytes, uint64_t after, uint64_t *edge)
{
    present_resources_t *resources = user;
    return resources && resources->te ? esp_display_present_te_sync_admit(resources->te, bytes, after, edge) : ESP_ERR_TIMEOUT;
}
static void te_start(void *user)
{
    present_resources_t *resources = user;
    esp_display_present_te_sync_record_tx_start(resources->te);
}
static void PRESENT_ISR_ATTR te_done(void *user, esp_err_t error)
{
    present_resources_t *resources = user;
    if (error == ESP_OK) {
        esp_display_present_te_sync_record_tx_done(resources->te);
    } else {
        esp_display_present_te_sync_abort_tx(resources->te);
    }
}
static bool PRESENT_ISR_ATTR te_wakeup(void *user, bool isr)
{
    present_resources_t *resources = user;
    if (isr) {
        return esp_display_present_engine_notify_te_isr(resources->presenter, present_now_us());
    }
    esp_display_present_engine_notify_te(resources->presenter, present_now_us());
    return false;
}

static size_t te_staging_bytes(uint32_t width, uint32_t height, unsigned rotation, size_t bpp)
{
    size_t row = (size_t)(rotation & 1 ? height : width) * bpp;
    size_t rows = row ? 32768 / row : 0;
    if (!rows) {
        rows = 1;
    }
    uint32_t physical_height = rotation & 1 ? width : height;
    if (rows > physical_height) {
        rows = physical_height;
    }
    return row * rows;
}

static esp_display_present_plan_memory_t preferred_external_memory(size_t bytes, size_t count)
{
#ifdef PRESENT_HOST
    (void)bytes;
    (void)count;
    return ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL;
#else
    if (count && bytes <= SIZE_MAX / count &&
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) >= bytes * count &&
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) >= bytes) {
        return ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL;
    }
    return ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA;
#endif
}
static size_t default_tiles(bool overlap, esp_display_present_plan_memory_t memory, size_t bytes, size_t staging)
{
#ifdef PRESENT_HOST
    (void)overlap;
    (void)memory;
    (void)bytes;
    (void)staging;
    return 2;
#else
    if (!overlap) {
        return 2;    /* V1 plain GRAM default. */
    }
    if (portNUM_PROCESSORS < 2) {
        return 1;
    }
    if (memory == ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL) {
        bytes = 0;
    }
    if (staging > SIZE_MAX - 128 * 1024 || bytes > (SIZE_MAX - staging - 128 * 1024) / 2) {
        return 1;
    }
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) >= bytes * 2 + staging + 128 * 1024 ? 2 : 1;
#endif
}

esp_err_t present_resources_plan(const esp_display_presenter_config_t *input, bool bounce, esp_display_present_plan_config_t *out)
{
    if (!input || !out || !input->width || !input->height ||
            input->pixel_format < ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 || input->pixel_format > ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB888 ||
            input->pixel_format != input->target.hw.input_pixel_format || input->max_damage_areas > UINT16_MAX ||
            input->target.drawbuf.buffers > 2 || input->target.drawbuf.te_compose_buffers > 2 ||
            input->target.fb.frame_buffer_count > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)bounce; /* Adapter queries authoritative RGB facts at creation. */
    const esp_display_present_hw_config_t *hw = &input->target.hw;
    if (hw->rotation != 0 && hw->rotation != 90 && hw->rotation != 180 && hw->rotation != 270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hw->swap_bytes && input->pixel_format != ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_display_present_panel_t panel_type = hw->panel_type;
    if (panel_type == ESP_DISPLAY_PRESENT_PANEL_AUTO && hw->io) {
        panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    }
    if (panel_type <= ESP_DISPLAY_PRESENT_PANEL_AUTO || panel_type > ESP_DISPLAY_PRESENT_PANEL_IO) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool framebuffer = panel_type != ESP_DISPLAY_PRESENT_PANEL_IO;
#if !defined(PRESENT_HOST) && !CONFIG_FREERTOS_UNICORE && !CONFIG_ESP_IPC_ENABLE
    if (framebuffer) {
        return ESP_ERR_NOT_SUPPORTED; /* Cannot join borrowed-panel ISRs safely. */
    }
#endif

    esp_display_present_mode_t mode = input->target.fb.mode;
    if (mode < ESP_DISPLAY_PRESENT_MODE_NONE || mode > ESP_DISPLAY_PRESENT_MODE_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    bool te_enabled = hw->te_enabled && hw->te_sync.gpio_num >= 0;
    if (mode == ESP_DISPLAY_PRESENT_MODE_AUTO) {
        mode = framebuffer ? ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL : te_enabled ? ESP_DISPLAY_PRESENT_MODE_TE_SYNC : ESP_DISPLAY_PRESENT_MODE_NONE;
    }
    /* No invented TE timing or silent fallback to unsynchronized output. */
    bool te = mode == ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
    if ((te && (framebuffer || !te_enabled)) ||
            (!framebuffer && mode != ESP_DISPLAY_PRESENT_MODE_NONE && !te)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    static const esp_display_present_present_mode_t modes[] = {
        ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE, ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_FULL,
        ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_ENGINE_MODE_TE_SYNC,
        ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_ENGINE_MODE_AUTO,
    };
    size_t bytes_per_pixel = input->pixel_format == ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 ? 2 : 3;
    if ((size_t)input->width > SIZE_MAX / input->height / bytes_per_pixel) {
        return ESP_ERR_INVALID_SIZE;
    }
    /* Keep exact logical capacities. Accelerator probe must fall back to CPU
     * when its aligned destination window does not fit; never invent padding. */
    size_t rows = input->target.drawbuf.lines;
    if (!rows) {
        rows = 32768 / ((size_t)input->width * bytes_per_pixel);
        if (!rows) {
            rows = 1;
        }
        if (rows > input->height) {
            rows = input->height;
        }
    }
    if (rows > input->height) {
        return ESP_ERR_INVALID_ARG;
    }
    bool partial = mode == ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL || mode == ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL || !framebuffer;
    size_t images = framebuffer ? (mode == ESP_DISPLAY_PRESENT_MODE_NONE ? 1 : mode == ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL || mode == ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL ? 3 : 2) : te ? (input->target.drawbuf.te_compose_buffers ? input->target.drawbuf.te_compose_buffers : 1) : 0;
    if (input->target.fb.frame_buffer_count && framebuffer && input->target.fb.frame_buffer_count != images) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t tile_bytes = (size_t)input->width * rows * bytes_per_pixel;
    esp_display_present_plan_memory_t tile_memory = input->target.drawbuf.in_psram ? preferred_external_memory(tile_bytes, input->target.drawbuf.buffers ? input->target.drawbuf.buffers : 1) : ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA;
    size_t staging = te ? te_staging_bytes(input->width, input->height, hw->rotation / 90, bytes_per_pixel) : 0;
    esp_display_present_plan_memory_t image_memory = te ? preferred_external_memory((size_t)input->width * input->height * bytes_per_pixel, images) : ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL;
    size_t reserved = staging;
    if (te && image_memory == ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA) {
        size_t image_bytes = (size_t)input->width * input->height * bytes_per_pixel;
        if (images > (SIZE_MAX - reserved) / image_bytes) {
            return ESP_ERR_INVALID_SIZE;
        }
        reserved += image_bytes * images;
    }
    bool async = framebuffer || te || hw->rotation != 0 || input->target.drawbuf.in_psram;
    size_t slot_bytes = tile_bytes;
    if (!framebuffer && !te && async) {
        if (tile_memory == ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA) {
            if (slot_bytes > SIZE_MAX / 2) {
                return ESP_ERR_INVALID_SIZE;
            }
            slot_bytes *= 2;
        } else {
            if (tile_bytes > (SIZE_MAX - reserved) / 2) {
                return ESP_ERR_INVALID_SIZE;
            }
            reserved += 2 * tile_bytes; /* PSRAM draw + internal DMA conversion. */
        }
    }
    size_t tiles = partial ? (input->target.drawbuf.buffers ? input->target.drawbuf.buffers :
                              default_tiles(framebuffer || te || async, tile_memory, slot_bytes, reserved)) : 0;
    *out = (esp_display_present_plan_config_t) {
        .presenter = {
            .backend_kind = framebuffer ? ESP_DISPLAY_PRESENT_ENGINE_FB : te ? ESP_DISPLAY_PRESENT_ENGINE_TE : ESP_DISPLAY_PRESENT_ENGINE_GRAM,
            .mode = framebuffer && mode == ESP_DISPLAY_PRESENT_MODE_NONE ? ESP_DISPLAY_PRESENT_MODE_DEFAULT : modes[mode],
            .transfer_timeout_ms = input->transfer_timeout_ms ? input->transfer_timeout_ms : 1000,
            .timeout_ms = input->pipeline_acquire_timeout_ms ? input->pipeline_acquire_timeout_ms : 1000,
            .background_service = async,
            .backend = {
                .width = input->width, .height = input->height, .tile_rows = rows,
                .render_contract = framebuffer && mode == ESP_DISPLAY_PRESENT_MODE_NONE ? ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT : ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
                .buffer_count = framebuffer ? images : tiles, .max_frames = 8,
                .format = input->pixel_format == ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 ? ESP_DISPLAY_PRESENT_FORMAT_RGB565 : ESP_DISPLAY_PRESENT_FORMAT_RGB888,
                .rotation = (esp_display_present_engine_rotation_t)(hw->rotation / 90), .async_copy = async,
                .fixed_logical_rotation = true,
                .allow_active_scanout_writes = framebuffer && mode == ESP_DISPLAY_PRESENT_MODE_NONE,
                .internal_tx_staging = !framebuffer && !te && input->target.drawbuf.in_psram,
                .te_gate = te ? (esp_display_present_te_gate_t)
                {
                    te_admit, te_start, te_done, NULL
} : (esp_display_present_te_gate_t)
                {
                    0
                },
                .panel_order = hw->swap_bytes ? ESP_DISPLAY_PRESENT_ORDER_SWAPPED : ESP_DISPLAY_PRESENT_ORDER_NATIVE,
                .in_place_convert = !framebuffer && !async && hw->swap_bytes,
            },
        },
        .additional_budget_bytes = sizeof(present_resources_t) + (te ? te_staging_bytes(input->width, input->height, hw->rotation / 90, bytes_per_pixel) + 64 + present_event_memory() + esp_display_present_te_sync_memory() : 0),
        .resources = {
            .tile_count = tiles, .image_count = images,
            .tile_memory = tile_memory,
            .image_memory = image_memory,
            .logical_memory = preferred_external_memory((size_t)input->width * input->height * bytes_per_pixel, 1),
        },
    };
    /* Opaque SPI IO cannot establish direct PSRAM DMA support: stage through
     * independent internal conversion slots instead of rejecting valid draw memory. */
    return ESP_OK;
}

static esp_err_t copy_execute(void *user, const esp_display_present_copy_request_t *request)
{
    present_resources_t *resources = user;
    present_lock_enter(&resources->lock, false);
    esp_display_present_executor_dispatch_t dispatch = resources->dispatch;
    present_lock_exit(&resources->lock, false);
    esp_display_present_executor_selection_t selection;
    esp_err_t error = esp_display_present_executor_run(&dispatch, request, &selection);
    present_lock_enter(&resources->lock, false);
    if (selection.executor) {
        if (selection.executor == resources->cpu) {
            ++resources->status.cpu_copies;
        } else {
            ++resources->status.hardware_copies;
        }
    }
    present_lock_exit(&resources->lock, false);
    return error; /* No CPU retry after an executor starts. */
}

esp_err_t present_resources_select_executor(present_resources_t *resources, esp_display_present_engine_rotation_t rotation)
{
    if (!resources || resources->status.closing || rotation < ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || rotation > ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_display_present_executor_t *primary = rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 ? resources->ppa : resources->dma2d;
    if (!primary) {
        primary = resources->ppa ? resources->ppa : resources->cpu;
    }
    esp_display_present_executor_caps_t caps;
    esp_err_t error = esp_display_present_executor_get_caps(primary, &caps);
    if (error != ESP_OK) {
        return error;
    }
    present_lock_enter(&resources->lock, false);
    resources->dispatch = (esp_display_present_executor_dispatch_t) {
        primary, primary == resources->cpu ? NULL : resources->cpu
    };
    resources->status.preferred_executor = caps.name;
    present_lock_exit(&resources->lock, false);
    return ESP_OK;
}

static esp_err_t free_resources(present_resources_t *resources)
{
    if (resources->scanout) {
        esp_err_t error = esp_display_present_lcd_scanout_destroy(resources->scanout);
        if (error != ESP_OK) {
            return error;
        }
        resources->scanout = NULL;
    }
    if (resources->spi) {
        esp_err_t error = esp_display_present_lcd_spi_destroy(resources->spi);
        if (error != ESP_OK) {
            return error;
        }
        resources->spi = NULL;
    }
    esp_display_present_executor_t **executors[] = {&resources->ppa, &resources->dma2d, &resources->cpu};
    for (size_t i = 0; i < 3; ++i) {
        if (*executors[i]) {
            esp_err_t error = esp_display_present_executor_destroy(*executors[i]);
            if (error != ESP_OK) {
                return error;
            }
            *executors[i] = NULL;
        }
    }
    present_lock_destroy(&resources->lock);
    present_free(resources);
    return ESP_OK;
}

esp_err_t present_resources_create(const esp_display_presenter_config_t *input, bool bounce,
                                   esp_display_present_scanout_vsync_fn on_vsync, void *vsync_user, present_resources_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    esp_display_present_plan_config_t config;
    esp_err_t error = present_resources_plan(input, bounce, &config);
    if (error != ESP_OK) {
        return error;
    }
    if (!input->target.hw.panel || (config.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_GRAM && !input->target.hw.io)) {
        return ESP_ERR_INVALID_ARG;
    }
    present_resources_t *resources = present_alloc(sizeof(*resources));
    if (!resources) {
        return ESP_ERR_NO_MEM;
    }
    if (!present_lock_init(&resources->lock)) {
        present_free(resources);
        return ESP_ERR_NO_MEM;
    }
    error = esp_display_present_executor_cpu_create(&resources->cpu);
    if (error != ESP_OK) {
        (void)free_resources(resources);
        return error;
    }
    resources->dispatch.primary = resources->cpu;
    resources->te_required = config.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_TE;
    resources->te_config = input->target.hw.te_sync;
    if (config.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_FB) {
        bool quarter = config.presenter.backend.rotation & 1;
        esp_display_present_lcd_scanout_config_t scan = {
            .panel = input->target.hw.panel,
            .kind = input->target.hw.panel_type == ESP_DISPLAY_PRESENT_PANEL_RGB ? ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE : ESP_DISPLAY_PRESENT_LCD_DSI,
            .width = quarter ? input->height : input->width, .height = quarter ? input->width : input->height,
            .format = config.presenter.backend.format, .buffer_count = config.resources.image_count,
            .byte_order = config.presenter.backend.panel_order,
            .rgb_bounce_buffer_mode = bounce, .retain_driver_scanout = true,
            .on_vsync = on_vsync, .vsync_user = vsync_user,
        };
        error = esp_display_present_lcd_scanout_create(&scan, &resources->scanout);
        if (error == ESP_OK) {
            size_t count;
            error = esp_display_present_lcd_scanout_get_buffers(resources->scanout, resources->buffers, 3, &count);
            if (error == ESP_OK && input->target.fb.frame_buffer_count) {
                for (size_t i = 0; i < count; ++i) {
                    if (input->target.fb.frame_buffers[i] != resources->buffers[i]) {
                        error = ESP_ERR_INVALID_ARG;
                    }
                }
            }
            config.presenter.backend.external_buffers = resources->buffers;
            config.presenter.backend.external_buffer_bytes = (size_t)input->width * input->height * (input->pixel_format == 0 ? 2 : 3);
            config.presenter.backend.initial_scanout_buffer = resources->buffers[0];
        }
        if (error == ESP_OK) {
            error = esp_display_present_lcd_scanout_get_interface(resources->scanout, &config.presenter.backend.panel);
        }
        if (error == ESP_OK) {
            error = esp_display_present_lcd_scanout_get_output_caps(resources->scanout, &config.output);
        }
    } else {
        size_t staging = resources->te_required ? te_staging_bytes(input->width, input->height,
                                                                   config.presenter.backend.rotation, input->pixel_format == ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 ? 2 : 3) : 0;
        bool direct_psram = false;
#ifdef ESP_LCD_SPI_IO_HAS_MEMORY_INFO
        esp_lcd_panel_io_spi_memory_info_t memory;
        if (esp_lcd_panel_io_spi_get_memory_info(input->target.hw.io, &memory) == ESP_OK) {
            direct_psram = memory.psram_dma_direct;
        }
#endif
        if (direct_psram) {
            staging = 0; /* Driver facts establish the configured direct path. */
        }
        if (resources->te_required && staging) {
            size_t row = (size_t)(config.presenter.backend.rotation & 1 ? input->height : input->width) *
                         (input->pixel_format == ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 ? 2 : 3);
            size_t rows = row ? staging / row : 0;
            uint32_t physical_height = config.presenter.backend.rotation & 1 ? input->width : input->height;
            /* TE timing assumes one continuous window. A staging band that cannot
             * hold the physical frame would split draw_bitmap. */
            if (rows < physical_height) {
                (void)free_resources(resources);
                return ESP_ERR_NOT_SUPPORTED;
            }
        }
        esp_display_present_lcd_spi_config_t spi = {.panel = input->target.hw.panel, .io = input->target.hw.io,
                                                    .psram_dma_direct = direct_psram,
                                                    .staging_bytes = staging, .staging_timeout_ms = input->transfer_timeout_ms,
                                                    .format = config.presenter.backend.format, .byte_order = config.presenter.backend.panel_order,
                                                    .max_inflight = config.resources.tile_count
                                                   };
        error = esp_display_present_lcd_spi_create(&spi, &resources->spi);
        if (error == ESP_OK) {
            error = esp_display_present_lcd_spi_get_interface(resources->spi, &config.presenter.backend.panel);
        }
        if (error == ESP_OK) {
            error = esp_display_present_lcd_spi_get_output_caps(resources->spi, &config.output);
        }
    }
    config.presenter.backend.copy_execute = config.presenter.backend.async_copy ? copy_execute : NULL;
    config.presenter.backend.copy_user = resources;
    config.presenter.backend.te_gate.user = resources;
    if (error == ESP_OK) {
        error = esp_display_present_plan_resolve(&config, &resources->status.plan);
    }
    if (error == ESP_OK) {
        error = esp_display_present_engine_create_planned(&config, &resources->presenter);
        /* Creation has not submitted pixels. The failed constructor releases
         * its pools and worker before returning; only automatic counts may shrink. */
        if (error == ESP_ERR_NO_MEM && input->target.drawbuf.buffers == 0 && config.resources.tile_count == 2) {
            config.resources.tile_count = 1;
            if (config.presenter.backend_kind != ESP_DISPLAY_PRESENT_ENGINE_FB) {
                config.presenter.backend.buffer_count = 1;
            }
            if (input->target.drawbuf.in_psram) {
                size_t bytes = (size_t)input->width * config.presenter.backend.tile_rows *
                               (input->pixel_format == ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 ? 2 : 3);
                config.resources.tile_memory = preferred_external_memory(bytes, 1);
            }
            error = esp_display_present_plan_resolve(&config, &resources->status.plan);
            if (error == ESP_OK) {
                error = esp_display_present_engine_create_planned(&config, &resources->presenter);
            }
        }
    }
    if (error != ESP_OK) {
        /* No hardware work has been submitted. Attach failure guarantees no
         * registration. Only CPU exists here; its destroy cannot fail. */
        esp_err_t cleanup = free_resources(resources);
        return cleanup == ESP_OK ? error : cleanup;
    }
    /* Optional accelerators are created last: no later initialization failure
     * needs to discard a hardware handle whose unregister may fail. A setup
     * failure uses CPU and is visible in the diagnostic snapshot. */
    resources->status.dma2d_create_result = config.presenter.backend.async_copy ? esp_display_present_executor_dma2d_create(&resources->dma2d) : ESP_ERR_NOT_SUPPORTED;
    resources->status.ppa_create_result = config.presenter.backend.async_copy ? esp_display_present_executor_ppa_create(&resources->ppa) : ESP_ERR_NOT_SUPPORTED;
    (void)present_resources_select_executor(resources, config.presenter.backend.rotation);
    resources->status.ready = resources->scanout == NULL && !resources->te_required;
    *out = resources;
    return ESP_OK;
}

esp_display_present_engine_t *present_resources_presenter(present_resources_t *resources)
{
    return resources ? resources->presenter : NULL;
}

esp_err_t present_resources_prepare(present_resources_t *resources, uint32_t timeout)
{
    if (!resources || resources->status.closing) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = ESP_OK;
    if (resources->te_required && !resources->te_armed) {
        if (resources->te) {
            error = esp_display_present_te_sync_destroy_checked(resources->te);
            if (error != ESP_OK) {
                return error;
            }
            resources->te = NULL;
        }
        error = esp_display_present_te_sync_create(&resources->te_config, false, &resources->te);
        if (error == ESP_OK) {
            esp_display_present_te_sync_set_wakeup(resources->te, te_wakeup, resources);
            resources->te_armed = true;
        }
    }
    if (error == ESP_OK && resources->scanout) {
        error = present_lcd_scanout_initialize(resources->scanout, timeout);
    }
    if (error == ESP_OK) {
        resources->status.ready = true;
    }
    return error;
}

esp_err_t present_resources_status(present_resources_t *resources, present_resource_status_t *out)
{
    if (!resources || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&resources->lock, false);
    *out = resources->status;
    present_lock_exit(&resources->lock, false);
    return ESP_OK;
}

esp_err_t present_resources_stop(present_resources_t *resources, uint32_t timeout)
{
    if (!resources) {
        return ESP_ERR_INVALID_ARG;
    }
    resources->status.closing = true;
    uint64_t start = present_now_ms();
    esp_err_t error = resources->presenter ? present_presenter_control_stop(resources->presenter, timeout) : ESP_OK;
    if (error == ESP_OK && resources->te) {
        error = esp_display_present_te_sync_destroy_budget(resources->te, present_remaining_ms(start, timeout));
        if (error == ESP_OK) {
            resources->te = NULL;
        }
    }
#ifndef PRESENT_HOST
    if (error == ESP_OK) {
        ESP_LOGI("present_v2", "executor=%s cpu_attempts=%" PRIu64 " hardware_attempts=%" PRIu64
                 " dma2d_create=%s ppa_create=%s",
                 resources->status.preferred_executor, resources->status.cpu_copies,
                 resources->status.hardware_copies, esp_err_to_name(resources->status.dma2d_create_result),
                 esp_err_to_name(resources->status.ppa_create_result));
    }
#endif
    return error;
}

esp_err_t present_resources_destroy(present_resources_t *resources, uint32_t timeout)
{
    if (!resources) {
        return ESP_ERR_INVALID_ARG;
    }
    resources->status.closing = true;
    if (resources->presenter) {
        esp_err_t error = present_resources_stop(resources, timeout);
        if (error != ESP_OK) {
            return error;
        }
        error = present_presenter_control_destroy(resources->presenter);
        if (error != ESP_OK) {
            return error;
        }
        resources->presenter = NULL;
    }
    return free_resources(resources);
}

esp_err_t present_resources_set_rotation(present_resources_t *resources, esp_display_present_engine_rotation_t rotation, uint32_t timeout)
{
    if (!resources || resources->status.closing) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = esp_display_present_engine_set_rotation(resources->presenter, rotation, timeout);
    if (error != ESP_OK) {
        return error;
    }
    esp_display_present_backend_caps_t caps;
    error = esp_display_present_engine_get_caps(resources->presenter, &caps);
    if (error == ESP_OK && resources->scanout) {
        uint32_t w = caps.draw_layout.width, h = caps.draw_layout.height;
        error = present_lcd_scanout_set_layout(resources->scanout, rotation & 1 ? h : w, rotation & 1 ? w : h);
    }
    return error;
}
