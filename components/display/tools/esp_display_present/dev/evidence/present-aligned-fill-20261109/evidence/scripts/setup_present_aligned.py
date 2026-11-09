from pathlib import Path
import shutil,json,subprocess,difflib
R=Path('/tmp/present-aligned-fill-20261109');R.mkdir(exist_ok=True)
base=Path('/tmp/present-s31-rgb-analysis-20261109/baseline/source');source=R/'source';shutil.copytree(base,source,dirs_exist_ok=True)
D=source/'components/display/tools/esp_display_present/dev';A=source/'components/display/tools/esp_lvgl_adapter/src/display/display_manager.c'
s=A.read_text();old='cfg->draw_buf_pixels = (size_t)profile->hor_res * profile->buffer_height;';assert s.count(old)==1
s=s.replace(old,'''/* Benchmark allocation alignment only: same logical-width tile as present. */
        size_t input_width = (profile->rotation == ESP_LV_ADAPTER_ROTATE_90 ||
                              profile->rotation == ESP_LV_ADAPTER_ROTATE_270) ? profile->ver_res : profile->hor_res;
        cfg->draw_buf_pixels = input_width * profile->buffer_height;''');A.write_text(s)
(R/'adapter-allocation-only.patch').write_text(''.join(difflib.unified_diff((base/A.relative_to(source)).read_text().splitlines(True),s.splitlines(True),fromfile='a/'+str(A.relative_to(source)),tofile='b/'+str(A.relative_to(source)))))
p=D/'test_apps/benchmark/main/bench_api.h';p.write_text(p.read_text()+'\nvoid bench_buffer_audit_start(lv_display_t *, const bench_case_t *);\nvoid bench_buffer_audit_stop(lv_display_t *);\nvoid bench_buffer_audit_report(void);\n')
p=D/'test_apps/benchmark/main/main.c';s=p.read_text().replace('        lv_demo_benchmark_set_end_cb(end_cb);','        bench_buffer_audit_start(disp, active);\n        lv_demo_benchmark_set_end_cb(end_cb);').replace('        memory_report("after_run", true);','        ESP_ERROR_CHECK(bench_lock());\n        bench_buffer_audit_stop(disp);\n        bench_unlock();\n        memory_report("after_run", true);').replace('        memory_report("after_close", false);','        memory_report("after_close", false);\n        bench_buffer_audit_report();');p.write_text(s)
p=D/'test_apps/benchmark/main/CMakeLists.txt';p.write_text(p.read_text().replace('SRCS main.c ${BRIDGE}','SRCS main.c buffer_audit.c ${BRIDGE}'))
p=D/'test_apps/benchmark/main/buffer_audit.c';p.write_text(r'''/* Benchmark-only buffer verification and nonempty refresh counters. */
#include <inttypes.h>
#include <string.h>
#include "bench_api.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
static struct {
    const bench_case_t *test;
    uint64_t refresh, rendered, flushes, pixels;
    uint32_t bytes, min_bytes, max_bytes, internal, psram;
    uint32_t expected, logical_w, logical_h, render_mode;
    int64_t start, elapsed;
} meter;
static void events(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_REFR_READY: meter.refresh++; break;
    case LV_EVENT_RENDER_READY: meter.rendered++; break;
    case LV_EVENT_FLUSH_START: {
        lv_display_t *d = lv_event_get_target(e);
        lv_draw_buf_t *b = lv_display_get_buf_active(d);
        assert(b && b->data);
        if (meter.expected) assert(b->data_size == meter.expected);
        if (!meter.flushes) {
            meter.bytes = b->data_size;
            meter.min_bytes = meter.max_bytes = b->data_size;
            meter.internal = esp_ptr_internal(b->data);
            meter.psram = esp_ptr_external_ram(b->data);
        }
        if (b->data_size < meter.min_bytes) meter.min_bytes = b->data_size;
        if (b->data_size > meter.max_bytes) meter.max_bytes = b->data_size;
        const lv_area_t *a = lv_event_get_param(e);
        if (a) meter.pixels += (uint64_t)lv_area_get_width(a) * lv_area_get_height(a);
        meter.flushes++;
        break;
    }
    default: break;
    }
}
void bench_buffer_audit_start(lv_display_t *d, const bench_case_t *c)
{
    memset(&meter, 0, sizeof(meter)); meter.test = c;
    meter.logical_w = lv_display_get_horizontal_resolution(d);
    meter.logical_h = lv_display_get_vertical_resolution(d);
    meter.render_mode = lv_display_get_render_mode(d);
    bool partial = c->mode == B_DOUBLE_PARTIAL || c->mode == B_TRIPLE_PARTIAL;
    bool full = c->mode == B_DOUBLE_FULL || c->mode == B_TRIPLE_FULL;
    if (partial) meter.expected = meter.logical_w * 50 * 2;
    if (full) meter.expected = meter.logical_w * meter.logical_h * 2;
    meter.start = esp_timer_get_time();
    lv_display_add_event_cb(d, events, LV_EVENT_ALL, NULL);
}
void bench_buffer_audit_stop(lv_display_t *d)
{
    meter.elapsed = esp_timer_get_time() - meter.start;
    lv_display_remove_event_cb_with_user_data(d, events, NULL);
}
void bench_buffer_audit_report(void)
{
    ESP_LOGI("BUFFER_AUDIT", "mode=%s rotation=%u logical_w=%u logical_h=%u render_mode=%u expected_bytes=%u active_bytes=%u min_bytes=%u max_bytes=%u internal=%u psram=%u input_tiles=%u compose=%u refresh=%"PRIu64" rendered=%"PRIu64" flushes=%"PRIu64" pixels=%"PRIu64" elapsed_us=%"PRIi64,
             meter.test->name, meter.test->rotation, meter.logical_w, meter.logical_h, meter.render_mode,
             meter.expected, meter.bytes, meter.min_bytes, meter.max_bytes, meter.internal, meter.psram,
             meter.test->tiles, meter.test->compose, meter.refresh, meter.rendered, meter.flushes, meter.pixels, meter.elapsed);
}
''')
# Keep original XIP settings on every board; this run isolates buffer policy.
for root in [R,R/'iram-off']:
 root.mkdir(exist_ok=True);(root/'deps').symlink_to('/tmp/present-five-board-20261109/rgb-retry/deps',target_is_directory=True) if not (root/'deps').exists() else None
shutil.copytree(source,R/'iram-off/source',dirs_exist_ok=True)
p=R/'iram-off/source/components/display/tools/esp_display_present/dev/boards/configs/s3-rgb.defaults';p.write_text(p.read_text()+'\n# CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM is not set\n')
ports={'s31-rgb':'/dev/ttyUSB0','s3-rgb':'/dev/ttyUSB1','p4-dsi':'/dev/ttyACM0','box3-spi':'/dev/ttyACM2'}
rev=subprocess.check_output(['git','-C','/home/xuxin/xuxin/esp-iot-solution-sensor-MR','rev-parse','HEAD'],text=True).strip()
(R/'scope.json').write_text(json.dumps(dict(revision=rev,ports=ports,scanout_profiles=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_PARTIAL','TRIPLE_PARTIAL'],angles=[0,90,180,270],input_tiles=1,tile_policy='logical_width*50*RGB565; adapter allocation only shim',clean_idf=True,original_xip_preserved=True,mosaico='expected original board MAC not yet identified; current software CDC busy'),indent=2)+'\n')
print(R)
