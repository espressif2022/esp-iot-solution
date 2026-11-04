#include "present_executor.h"
#include "present_os.h"
#ifndef PRESENT_HOST
#include "soc/soc_caps.h"
#include "esp_idf_version.h"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6,2,0)
#include "esp_async_memcpy.h"
#include "esp_memory_utils.h"
extern unsigned present_gdma_test_burst;
typedef struct { esp_display_present_executor_t base; present_mutex_t *gate; async_memcpy_handle_t dma; } gdma_t;
static esp_err_t gdma_caps(void *ctx, esp_display_present_executor_caps_t *out) {
 (void)ctx; *out=(esp_display_present_executor_caps_t){.name="gdma",.format_mask=3,.rotation_mask=1,.base_alignment=64,.destination_capacity_alignment=1,.stride_alignment_bytes=1,.address_dependent_constraints=true};return ESP_OK;
}
static esp_err_t gdma_probe(void *ctx,const esp_display_present_copy_request_t *r,esp_display_present_executor_reason_t *why) {
 (void)ctx;*why=ESP_DISPLAY_PRESENT_EXEC_INVALID_REQUEST;
 esp_err_t e=esp_display_present_pixel_validate_request(r);if(e!=ESP_OK)return e;
 if(r->rotation!=ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || r->source.layout.byte_order!=r->destination.layout.byte_order){*why=ESP_DISPLAY_PRESENT_EXEC_TRANSFORM;return ESP_ERR_NOT_SUPPORTED;}
 size_t bpp=r->source.layout.format==ESP_DISPLAY_PRESENT_FORMAT_RGB565?2:3;
 size_t width=r->source_area.width*bpp;
 if(r->source.layout.stride_bytes!=width || r->destination.layout.stride_bytes!=width){*why=ESP_DISPLAY_PRESENT_EXEC_LAYOUT;return ESP_ERR_NOT_SUPPORTED;}
 unsigned char *src=(unsigned char*)r->source.pixels+r->source_area.y*r->source.layout.stride_bytes+r->source_area.x*bpp;
 unsigned char *dst=(unsigned char*)r->destination.pixels+r->destination_y*r->destination.layout.stride_bytes+r->destination_x*bpp;
 size_t n=width*r->source_area.height;
 if(src==dst){*why=ESP_DISPLAY_PRESENT_EXEC_IN_PLACE;return ESP_ERR_NOT_SUPPORTED;}
 if(((uintptr_t)src%64)||((uintptr_t)dst%64)||n%64||n<128){*why=ESP_DISPLAY_PRESENT_EXEC_ALIGNMENT;return ESP_ERR_NOT_SUPPORTED;}
 if(!(esp_ptr_dma_capable(src)||esp_ptr_external_ram(src)) || !(esp_ptr_dma_capable(dst)||esp_ptr_external_ram(dst))){*why=ESP_DISPLAY_PRESENT_EXEC_MEMORY;return ESP_ERR_NOT_SUPPORTED;}
 *why=ESP_DISPLAY_PRESENT_EXEC_SUPPORTED;return ESP_OK;
}
static esp_err_t gdma_execute(void *ctx,const esp_display_present_copy_request_t *r) {
 gdma_t *g=ctx;esp_display_present_executor_reason_t why;esp_err_t e=gdma_probe(ctx,r,&why);if(e!=ESP_OK)return e;
 size_t bpp=r->source.layout.format==ESP_DISPLAY_PRESENT_FORMAT_RGB565?2:3;
 unsigned char *src=(unsigned char*)r->source.pixels+r->source_area.y*r->source.layout.stride_bytes+r->source_area.x*bpp;
 unsigned char *dst=(unsigned char*)r->destination.pixels+r->destination_y*r->destination.layout.stride_bytes+r->destination_x*bpp;
 (void)present_mutex_take(g->gate,UINT32_MAX);
 e=esp_memcpy_blocking(g->dma,dst,src,r->source_area.width*r->source_area.height*bpp,-1);
 present_mutex_give(g->gate);return e;
}
static esp_err_t gdma_destroy(void *ctx) { gdma_t *g=ctx;esp_err_t e=esp_async_memcpy_uninstall(g->dma);if(e!=ESP_OK)return e;present_mutex_destroy(g->gate);present_free(g);return ESP_OK; }
static const esp_display_present_executor_ops_t gdma_ops={gdma_caps,gdma_probe,gdma_execute,gdma_destroy};
esp_err_t esp_display_present_executor_gdma_create(esp_display_present_executor_t **out) {
 if (!out) { return ESP_ERR_INVALID_ARG; }
 *out = NULL;
 gdma_t *g=present_alloc(sizeof(*g));if(!g)return ESP_ERR_NO_MEM;
 g->gate=present_mutex_create();if(!g->gate){present_free(g);return ESP_ERR_NO_MEM;}
 async_memcpy_config_t cfg=ASYNC_MEMCPY_DEFAULT_CONFIG();cfg.backlog=2;cfg.dma_burst_size=present_gdma_test_burst;
 esp_err_t e=esp_async_memcpy_install_gdma_ahb(&cfg,&g->dma);
 if(e!=ESP_OK){present_mutex_destroy(g->gate);present_free(g);return e;}
 g->base=(esp_display_present_executor_t){&gdma_ops,g};*out=&g->base;return ESP_OK;
}
#else
esp_err_t esp_display_present_executor_gdma_create(esp_display_present_executor_t **out) { if (!out) { return ESP_ERR_INVALID_ARG; }
 *out = NULL;return ESP_ERR_NOT_SUPPORTED; }
#endif
