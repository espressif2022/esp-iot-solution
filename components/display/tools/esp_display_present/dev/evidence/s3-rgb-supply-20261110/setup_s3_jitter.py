from pathlib import Path
import shutil,json,hashlib,subprocess
original=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');base=Path('/tmp/present-s3-jitter-source-20261110');c=base/'components/display/tools/esp_display_present';r=Path('/tmp/present-s3-jitter-work-20261110')
assert not base.exists() and not r.exists()
shutil.copytree(original,c,ignore=shutil.ignore_patterns('evidence','build','build-*','__pycache__','.git','udo nmcli*'))
for name in ['esp_lv_present','esp_lvgl_adapter']:(c.parent/name).symlink_to(original.parent/name,target_is_directory=True)
r.mkdir();(r/'deps').symlink_to(Path('/tmp/present-xip-matrix-20261110/deps').resolve(),target_is_directory=True)
(r/'s3-rgb').mkdir();(r/'s3-rgb/backup').symlink_to(Path('/tmp/present-xip-matrix-20261110/s3-rgb/backup').resolve(),target_is_directory=True)
(r/'hardware-policy.json').write_text(json.dumps({'restore_original':False,'reason':'用户要求保留测试固件'}))
# Independent test-only observer copy; original IDF and completed scores stay untouched.
overlay=r/'driver-overlay';shutil.copytree('/tmp/present-xip-matrix-20261110/driver-overlay',overlay)
h=overlay/'esp_lcd/include/esp_lcd_present_trace.h';s=h.read_text().replace('uint32_t pending, bounce_bytes, kind;','uint32_t pending, bounce_bytes, kind;\n    uint32_t supply_frames, supply_short, supply_restarts, supply_min_eofs, supply_expected;');s+='\nvoid esp_lcd_present_trace_rgb_supply(void *panel, unsigned got, unsigned expected, unsigned restart);\n';h.write_text(s)
p=overlay/'esp_lcd/src/esp_lcd_present_trace.c';s=p.read_text();pos=s.index('void esp_lcd_present_trace_reset(void)');s=s[:pos]+'''void TRACE_IRAM esp_lcd_present_trace_rgb_supply(void *panel, unsigned got, unsigned expected, unsigned restart)
{
    ENTER();
    if ((uintptr_t)panel == trace.view.panel) {
        if (!trace.view.supply_frames || got < trace.view.supply_min_eofs) trace.view.supply_min_eofs = got;
        ++trace.view.supply_frames;
        trace.view.supply_short += got < expected;
        trace.view.supply_restarts += restart != 0;
        trace.view.supply_expected = expected;
    }
    EXIT();
}

'''+s[pos:];needle='    for (unsigned i = 0; i < s.saved; ++i) {';s=s.replace(needle,'''    printf("RGB_SUPPLY mode=%s rotation=%u frames=%u short_frames=%u restarts=%u min_eofs=%u expected_eofs=%u\\n", mode, rotation, (unsigned)s.supply_frames, (unsigned)s.supply_short, (unsigned)s.supply_restarts, (unsigned)s.supply_min_eofs, (unsigned)s.supply_expected);
'''+needle);p.write_text(s)
p=overlay/'esp_lcd/rgb/esp_lcd_panel_rgb.c';s=p.read_text();a=s.index('static IRAM_ATTR void lcd_rgb_panel_try_restart_transmission(');z=s.index('#endif // RGB_LCD_NEEDS_SEPARATE_RESTART_LINK',a);part=s[a:z].replace('    bool do_restart = false;', '    bool do_restart = false;\n    unsigned trace_eofs = panel->bb_eof_count;');part=part.replace('    panel->bb_eof_count = 0;','    trace_eofs = panel->bb_eof_count;\n    panel->bb_eof_count = 0;');part=part.replace('    if (!do_restart) {','    esp_lcd_present_trace_rgb_supply(&panel->base, trace_eofs, panel->expect_eof_count, do_restart);\n    if (!do_restart) {');s=s[:a]+part+s[z:];p.write_text(s)
m=json.loads((overlay/'trace-manifest.json').read_text());m['files_sha256']={str(p.relative_to(overlay)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((overlay/'esp_lcd').rglob('*')) if p.is_file()};m['diagnostic_purpose']='S3 bounce EOF shortage and driver existing auto-restarts; count only';(overlay/'trace-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
# Scene-independent label on LVGL top layer, not part of official score matrix.
p=c/'dev/test_apps/benchmark/main/main.c';s=p.read_text().replace('        bench_unlock();\n        ESP_LOGI(TAG, "RUN', '''        lv_obj_t *identity = lv_label_create(lv_display_get_layer_top(disp));
        lv_label_set_text_fmt(identity, "%s | XIP OFF | %s | %u deg", BENCH_IMPLEMENTATION, active->name, active->rotation);
        lv_obj_set_style_bg_opa(identity, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(identity, lv_color_hex(0x102030), 0);
        lv_obj_set_style_text_color(identity, lv_color_hex(0xffff00), 0);
        lv_obj_align(identity, LV_ALIGN_TOP_MID, 0, 0);
        bench_unlock();
        ESP_LOGI(TAG, "RUN''');p.write_text(s)
# Print one supply snapshot per scene transition, after the ISR and outside its critical section.
# Full suite report remains available; no per-frame logging or changed control flow.
lvgl=Path('/tmp/present-xip-matrix-20261110/s3-rgb/benchmark/present-xip-off/lvgl').resolve();print('SHADOW',c,'WORK',r,'LVGL',lvgl)
