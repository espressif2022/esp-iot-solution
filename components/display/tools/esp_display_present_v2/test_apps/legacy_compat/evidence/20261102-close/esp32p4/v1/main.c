/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <string.h>
#include "esp_display_present.h"
#include "esp_lv_present.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "demos/lv_demos.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include "board_qspi.h"
#define WIDTH 480
#define HEIGHT 480
#else
#include "board.h"
#endif
static const char *TAG="compat_probe";
static bool benchmark_done;
static void check(esp_err_t error, const char *operation)
{
    if(error==ESP_OK) return;
    ESP_LOGE(TAG,"FAIL impl=%s operation=%s error=%s; resources retained",PROBE_IMPL,operation,esp_err_to_name(error));
    for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
static esp_display_presenter_config_t configuration(esp_lcd_panel_handle_t panel,esp_lcd_panel_io_handle_t io,
                                                    esp_display_present_mode_t mode,unsigned compose,unsigned rotation)
{
    return (esp_display_presenter_config_t){.width=WIDTH,.height=HEIGHT,.max_damage_areas=32,
        .transfer_timeout_ms=3000,.pipeline_acquire_timeout_ms=3000,
        .target={.hw={.panel=panel,.io=io,
#if CONFIG_IDF_TARGET_ESP32S31
            .panel_type=ESP_DISPLAY_PRESENT_PANEL_IO,.swap_bytes=true,
            .te_enabled=mode==ESP_DISPLAY_PRESENT_MODE_TE_SYNC,
            .te_sync={.gpio_num=43,.bus_freq_hz=40000000,.data_lines=4},
#elif CONFIG_IDF_TARGET_ESP32S3
            .panel_type=ESP_DISPLAY_PRESENT_PANEL_RGB,
#else
            .panel_type=ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI,
#endif
            .rotation=rotation},.fb={.mode=mode},.drawbuf={.lines=16,.buffers=2,.te_compose_buffers=compose}}};
}
static void raster(esp_display_presenter_t *presenter,unsigned frame)
{
    size_t count;bool full;
    check(esp_display_presenter_begin_next_frame(presenter,NULL,NULL,0,&count,&full),"begin");
    esp_display_presenter_caps_t caps;
    check(esp_display_presenter_get_caps(presenter,&caps),"caps");
    for(unsigned y=0;y<caps.height;){
        esp_display_presenter_buffer_t buffer;
        check(esp_display_presenter_acquire_buffer(presenter,&buffer),"acquire");
        size_t rows=caps.height-y;
        if(caps.contract==ESP_DISPLAY_PRESENT_CONTRACT_PARTITION)
            check(buffer.resolve_rows(buffer.resolve_rows_ctx,buffer.lease_id,caps.width*2,rows,&rows),"rows");
        for(unsigned yy=0;yy<rows;++yy) for(unsigned x=0;x<caps.width;++x){
            unsigned gy=y+yy;
            uint16_t color=x<caps.width/3?0xf800:x<caps.width*2/3?0x07e0:0x001f;
            if(x>=(frame*13)%caps.width && x<(frame*13)%caps.width+12) color=0xffff;
            if(gy<8) color=(frame&1)?0xffff:0;
            ((uint16_t*)buffer.surface.pixels)[yy*caps.width+x]=color;
        }
        esp_display_present_area_t area={0,y,caps.width-1,y+rows-1};
        check(esp_display_presenter_submit_buffer(presenter,&buffer,&area,caps.width*2),"submit");
        y+=rows;
    }
    const esp_display_presenter_submit_t submit = {.coverage=ESP_DISPLAY_PRESENT_COVERAGE_FULL};
    check(esp_display_presenter_commit_frame(presenter,&submit),"commit");
}
static void cancel_one(esp_display_presenter_t *presenter)
{
    size_t count;bool full;
    check(esp_display_presenter_begin_next_frame(presenter,NULL,NULL,0,&count,&full),"cancel begin");
    esp_display_presenter_caps_t caps;
    check(esp_display_presenter_get_caps(presenter,&caps),"cancel caps");
    esp_display_presenter_buffer_t buffer;
    check(esp_display_presenter_acquire_buffer(presenter,&buffer),"cancel acquire");
    unsigned rows=caps.contract==ESP_DISPLAY_PRESENT_CONTRACT_PARTITION?1:caps.height;
    memset(buffer.surface.pixels,0,(size_t)caps.width*rows*2);
    esp_display_present_area_t area={0,0,caps.width-1,rows-1};
    check(esp_display_presenter_submit_buffer(presenter,&buffer,&area,caps.width*2),"cancel submit");
    esp_display_presenter_cancel_frame(presenter);
}
static void run_case(esp_lcd_panel_handle_t panel,esp_lcd_panel_io_handle_t io,
                     esp_display_present_mode_t mode,unsigned compose,unsigned rotation)
{
    esp_display_presenter_config_t config=configuration(panel,io,mode,compose,rotation);
    esp_display_presenter_t *presenter;
    check(esp_display_presenter_create(&config,&presenter),"create");
    ESP_LOGI(TAG,"CASE impl=%s mode=%d compose=%u rotation=%u",PROBE_IMPL,mode,compose,rotation);
    cancel_one(presenter);
    uint64_t start=esp_timer_get_time();
    for(unsigned frame=0;frame<12;++frame) {
        raster(presenter,frame);
        if(!frame) ESP_LOGI(TAG,"CANCEL_RESUME_PASS impl=%s mode=%d",PROBE_IMPL,mode);
    }
    check(esp_display_presenter_quiesce(presenter,5000),"drain");
    esp_display_presenter_frame_status_t status;
    check(esp_display_presenter_get_frame_status(presenter,&status),"status");
    ESP_LOGI(TAG,"PASS impl=%s mode=%d compose=%u rotation=%u frames=12 elapsed_us=%"PRIu64" submitted=%"PRIu64" completed=%"PRIu64" heap=%u",
        PROBE_IMPL,mode,compose,rotation,(uint64_t)esp_timer_get_time()-start,status.last_submitted_frame,status.completed_transfer_frame,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    check(esp_display_presenter_stop(presenter),"stop");
    check(esp_display_presenter_delete(presenter),"delete");
}
static void summary(const lv_demo_benchmark_summary_t *s)
{
    check(s->valid_scene_cnt > 0 ? ESP_OK : ESP_FAIL,"benchmark scene sampling");
    for(lv_demo_benchmark_scene_dsc_t *scene=s->scenes;scene->create_cb;++scene)
        if(scene->measurement_cnt) ESP_LOGI(TAG,"SCENE impl=%s name=\"%s\" fps=%u cpu=%u render=%u flush=%u",PROBE_IMPL,scene->name,
            scene->fps_avg,scene->cpu_avg_usage,scene->render_avg_time,scene->flush_avg_time);
    ESP_LOGI(TAG,"BENCH_DONE impl=%s scenes=%"PRId32" fps=%"PRId32" cpu=%"PRId32" render=%"PRId32" flush=%"PRId32,
        PROBE_IMPL,s->valid_scene_cnt,s->total_avg_fps/s->valid_scene_cnt,s->total_avg_cpu/s->valid_scene_cnt,
        s->total_avg_render_time/s->valid_scene_cnt,s->total_avg_flush_time/s->valid_scene_cnt);
    benchmark_done=true;
}
static void run(void *unused)
{
    (void)unused;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io=NULL;
#if CONFIG_IDF_TARGET_ESP32S31
    check(qspi_board_create(&panel,&io),"qspi board");
    run_case(panel,io,ESP_DISPLAY_PRESENT_MODE_NONE,0,0);
    run_case(panel,io,ESP_DISPLAY_PRESENT_MODE_TE_SYNC,1,0);
    run_case(panel,io,ESP_DISPLAY_PRESENT_MODE_TE_SYNC,2,90);
#else
    check(board_panel_create(&panel,3),"scanout board");
    const esp_display_present_mode_t modes[]={ESP_DISPLAY_PRESENT_MODE_NONE,ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL,
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL,ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT,ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL,
        ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) run_case(panel,io,modes[i],0,0);
#endif
    esp_display_presenter_config_t config=configuration(panel,io,ESP_DISPLAY_PRESENT_MODE_AUTO,0,0);
#if CONFIG_IDF_TARGET_ESP32S31
    config.target.hw.te_enabled=false;
#endif
    config.target.drawbuf.lines=50;
    esp_display_presenter_t *presenter;
    check(esp_display_presenter_create(&config,&presenter),"benchmark create");
    lv_display_t *display;
    check(esp_lv_present_start(presenter,&display),"lvgl start");
    lv_demo_benchmark_set_end_cb(summary);
    lv_demo_benchmark();
    ESP_LOGI(TAG,"BENCH_START impl=%s width=%u height=%u",PROBE_IMPL,WIDTH,HEIGHT);
    uint64_t start=esp_timer_get_time();
    while(!benchmark_done && esp_timer_get_time()-start<240000000){
        uint32_t delay=lv_timer_handler();
        if(delay==LV_NO_TIMER_READY) delay=1;
        else if(delay>500) delay=500;
        if(!delay) taskYIELD(); else vTaskDelay(pdMS_TO_TICKS(delay));
    }
    check(benchmark_done?ESP_OK:ESP_ERR_TIMEOUT,"benchmark completion");
    check(esp_display_presenter_quiesce(presenter,5000),"benchmark drain");
    check(esp_lv_present_stop(),"lvgl stop");
    check(esp_display_presenter_delete(presenter),"benchmark delete");
    ESP_LOGI(TAG,"ALL_PASS impl=%s",PROBE_IMPL);
    for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
void app_main(void)
{
    ESP_LOGI(TAG,"START impl=%s",PROBE_IMPL);
    if(xTaskCreate(run,"compat_probe",12288,NULL,6,NULL)!=pdPASS) check(ESP_ERR_NO_MEM,"task");
}
