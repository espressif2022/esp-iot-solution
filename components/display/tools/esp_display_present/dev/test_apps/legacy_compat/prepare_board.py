#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Prepare an isolated, offline V2 board probe. Never connect or flash."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, choices=('esp32p4', 'esp32s3', 'esp32s31'))
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--lvgl-dir', required=True, type=Path)
    parser.add_argument('--skip-te', action='store_true', help='Explicitly exclude TE smoke cases, not a TE pass')
    args = parser.parse_args()
    output = args.output.resolve()
    lvgl = args.lvgl_dir.resolve()
    if output.exists():
        parser.error('output must not exist')
    if not (lvgl / 'lvgl.h').is_file():
        parser.error('local LVGL source required; nothing is downloaded')
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
set(PROBE_IMPL "v2")
set(EXTRA_COMPONENT_DIRS "{ROOT.parent}" "{ROOT}/test_apps/boards/deps"
 "{ROOT.parent.parent}/esp_lv_present")
set(COMPONENTS main)
include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
project(pv2_compat_probe)
'''
    (output / 'CMakeLists.txt').write_text(cmake)
    main = f'''idf_component_register(SRCS "{ROOT}/test_apps/legacy_compat/board_probe/main.c" "{ROOT}/test_apps/boards/main/{board}"
 INCLUDE_DIRS "{ROOT}/test_apps/boards/main"
 REQUIRES esp_display_present esp_lv_present lvgl esp_lcd esp_timer esp_psram esp_driver_gpio esp_driver_i2c {deps})
target_compile_definitions(${{COMPONENT_LIB}} PRIVATE "PROBE_IMPL=\\\"${{PROBE_IMPL}}\\\"")
'''
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
