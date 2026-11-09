from pathlib import Path
import shutil,json
R=Path('/tmp/present-s31-layout-20261109');old=Path('/tmp/present-s31-dispatch-20261109');R.mkdir(exist_ok=True)
shutil.copytree(old/'lvgl',R/'lvgl',dirs_exist_ok=True)
p=R/'lvgl/demos/benchmark/lv_demo_benchmark.c';s=p.read_text();a=s.index('static lv_demo_benchmark_scene_dsc_t scenes[] = {');b=s.index('\nstatic uint32_t scene_act;',a)
s=s[:a]+'''static lv_demo_benchmark_scene_dsc_t scenes[] = {
    {.name = "Screen sized text", .scene_time = 18000, .create_cb = screen_sized_text_cb},
    {.name = "", .create_cb = NULL}
};
'''+s[b:];p.write_text(s)
for name in ['baseline','early']:
 r=R/name;shutil.copytree(old/'off/source',r/'source',dirs_exist_ok=True)
 (r/'deps').symlink_to((old/'off/deps').resolve(),target_is_directory=True)
 m=r/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main/main.c';s=m.read_text().replace('const unsigned repeats = 4;','const unsigned repeats = 2;').replace('inline_mode = repeat == 1 || repeat == 2;','inline_mode = false;');m.write_text(s)
 if name=='early':
  p=r/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/CMakeLists.txt';p.write_text(p.read_text()+'''\n# Layout diagnostic only: force early extraction of the same LVGL blend objects.
if(PRESENT_IMPLEMENTATION STREQUAL "present")
 target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE "-Wl,-u,lv_draw_sw_blend_color_to_rgb565" "-Wl,-u,lv_draw_sw_blend_color_to_rgb888")
endif()
''')
(R/'scope.json').write_text(json.dumps(dict(parent=str(old),xip='off',scene='Screen sized text',seconds=18,angle=0,repeats=2,inline=False,tiles=1,bounce_lines=10,only_intervention='Force undefined RGB565 and RGB888 blend symbols before archive extraction',formal_matrix=False),indent=2))
print(R)
