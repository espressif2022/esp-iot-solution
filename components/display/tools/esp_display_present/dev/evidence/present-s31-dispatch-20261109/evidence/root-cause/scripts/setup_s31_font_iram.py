from pathlib import Path
import shutil,json
old=Path('/tmp/present-s31-layout-20261109');R=Path('/tmp/present-s31-font-iram-20261109');R.mkdir(exist_ok=True);shutil.copytree(old/'lvgl',R/'lvgl',dirs_exist_ok=True)
for n in ['baseline','pad24']:
 r=R/n;src=old/'baseline' if n=='baseline' else Path('/tmp/present-s31-stable-20261109/pad24');shutil.copytree(src/'source',r/'source',dirs_exist_ok=True);(r/'deps').symlink_to((old/'baseline/deps').resolve(),target_is_directory=True)
 m=r/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main';p=m/'CMakeLists.txt';s=p.read_text().replace(' REQUIRES ${DEPS}', ' LDFRAGMENTS "font_bitmap.lf" REQUIRES ${DEPS}');assert s!=p.read_text();p.write_text(s)
 (m/'font_bitmap.lf').write_text('[mapping:font_bitmap_diagnostic]\narchive: liblvgl.a\nentries:\n    lv_font_fmt_txt:lv_font_get_bitmap_fmt_txt (noflash)\n')
 p=m.parent/'CMakeLists.txt';s=p.read_text()
 if n=='pad24':s=s.replace(str(src),str(r))
 else:s=s.replace('include($ENV{IDF_PATH}/tools/cmake/project.cmake)',f'add_compile_options("-fmacro-prefix-map={r}={old/"baseline"}")\ninclude($ENV{{IDF_PATH}}/tools/cmake/project.cmake)')
 p.write_text(s)
(R/'scope.json').write_text(json.dumps(dict(experiment='Move only lv_font_get_bitmap_fmt_txt to IRAM through an application linker fragment; same mapping for present and adapter',source_unchanged=True,idf_patched=False,scanout_unchanged=True,xip='off',tiles=1,bounce_lines=10,scene='Screen sized text',seconds=18),indent=2))
print(R)
