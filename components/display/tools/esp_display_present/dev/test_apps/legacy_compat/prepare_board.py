#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Prepare an isolated, offline display probe. Never connect or flash."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, choices=('esp32p4', 'esp32s3', 'esp32s31'))
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--lvgl-dir', required=True, type=Path)
    parser.add_argument('--reference-components', type=Path,
                        help='Directory containing archived V1 esp_display_present and esp_lv_present components')
    parser.add_argument('--skip-te', action='store_true', help='Explicitly exclude TE smoke cases, not a TE pass')
    parser.add_argument('--mode-matrix', action='store_true',
                        help='Prepare the V1/current mode and rotation comparison, without LVGL benchmark')
    args = parser.parse_args()
    if args.mode_matrix and args.skip_te:
        parser.error('--skip-te is not applicable to the mode matrix')
    output = args.output.resolve()
    lvgl = args.lvgl_dir.resolve()
    component_root = args.reference_components.resolve() if args.reference_components else ROOT.parent.parent
    presenter = component_root / 'esp_display_present'
    binding = component_root / 'esp_lv_present'
    implementation = 'v1' if args.reference_components else 'v2'
    if output.exists():
        parser.error('output must not exist')
    if not (lvgl / 'lvgl.h').is_file():
        parser.error('local LVGL source required; nothing is downloaded')
    if not (presenter / 'CMakeLists.txt').is_file() or not (binding / 'CMakeLists.txt').is_file():
        parser.error('component directory must contain esp_display_present and esp_lv_present')
    board, deps = {
        'esp32p4': ('board_p4.c', 'esp_lcd_ek79007 esp_hw_support'),
        'esp32s3': ('board_s3.c', 'esp_lcd_gc9503 esp_lcd_panel_io_additions esp_io_expander_tca9554'),
        'esp32s31': ('board_qspi.c', 'esp_lcd_co5300 esp_driver_spi efuse'),
    }[args.target]
    (output / 'main').mkdir(parents=True)
    (output / 'components').mkdir()
    (output / 'components/lvgl').symlink_to(lvgl, target_is_directory=True)
    cmake = f'''cmake_minimum_required(VERSION 3.16)
set(ENV{{IDF_COMPONENT_MANAGER}} "0")
set(PROBE_IMPL "{implementation}")
set(EXTRA_COMPONENT_DIRS "{presenter}" "{ROOT}/test_apps/boards/deps"
 "{binding}")
set(COMPONENTS main)
include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
project(pv2_compat_probe)
'''
    if args.mode_matrix:
        cmake += 'target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE "-Wl,--wrap=esp_lcd_panel_draw_bitmap")\n'
    (output / 'CMakeLists.txt').write_text(cmake)
    source = 'mode_matrix.c' if args.mode_matrix else 'main.c'
    main = f'''idf_component_register(SRCS "{ROOT}/test_apps/legacy_compat/board_probe/{source}" "{ROOT}/test_apps/boards/main/{board}"
 INCLUDE_DIRS "{ROOT}/test_apps/boards/main"
 REQUIRES esp_display_present esp_lv_present lvgl esp_lcd esp_timer esp_psram esp_driver_gpio esp_driver_i2c {deps})
target_compile_definitions(${{COMPONENT_LIB}} PRIVATE "PROBE_IMPL=\\\"${{PROBE_IMPL}}\\\"")
'''
    if args.mode_matrix:
        main += 'target_compile_definitions(${COMPONENT_LIB} PRIVATE "TEST_IMPL=\\\"${PROBE_IMPL}\\\"")\n'
    if args.skip_te:
        main += 'target_compile_definitions(${COMPONENT_LIB} PRIVATE PROBE_SKIP_TE=1)\n'
    (output / 'main/CMakeLists.txt').write_text(main)
    defaults = (ROOT / 'test_apps/boards/sdkconfig.defaults').read_text()
    defaults += '\n' + (ROOT / ('test_apps/boards/sdkconfig.defaults.' + args.target)).read_text()
    defaults += '\n' + (ROOT / 'test_apps/legacy_compat/board_probe/sdkconfig.defaults').read_text()
    (output / 'sdkconfig.defaults').write_text(defaults)
    (output / 'partitions.csv').write_text('nvs,data,nvs,0x9000,0x6000,\nphy_init,data,phy,0xf000,0x1000,\nfactory,app,factory,0x10000,0x300000,\n')
    print(output)


if __name__ == '__main__':
    main()
