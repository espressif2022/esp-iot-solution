#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Stage an original example with opt-in V2 legacy components; never flash/fetch."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
REPO = ROOT.parents[3]
EXAMPLES = REPO / 'examples/display/gui'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--example', required=True, choices=(
        'lvgl_present_benchmark', 'gsp_lvgl_present_handoff', 'gsp_showcase'))
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--gsp-package', type=Path, help='Exact local esp-gsp package; no dependency download')
    args = parser.parse_args()
    source = EXAMPLES / args.example
    output = args.output.resolve()
    if output.exists():
        parser.error('output must not exist; choose a fresh isolated directory')
    managed = source / 'managed_components'
    if not (managed / 'lvgl__lvgl/lvgl.h').exists():
        managed = EXAMPLES / 'gsp_lvgl_present_handoff/managed_components'
    if not (managed / 'lvgl__lvgl/lvgl.h').exists():
        parser.error('local cached LVGL missing; this tool does not download dependencies')
    gsp = args.example.startswith('gsp_')
    gsp_package = (args.gsp_package.resolve() if args.gsp_package else managed / 'espressif__esp-gsp')
    if args.gsp_package and not gsp:
        parser.error('--gsp-package requires a GSP example')
    if gsp and not (gsp_package / 'cmake/gsp_bundle.cmake').exists():
        parser.error('local cached esp-gsp package missing')
    shutil.copytree(source, output, ignore=shutil.ignore_patterns(
        'build', 'build-*', 'managed_components', 'sdkconfig', 'dependencies.lock', '.git'))
    components = output / 'components'
    components.mkdir(exist_ok=True)
    if gsp and args.gsp_package:
        (components / 'espressif__esp-gsp').symlink_to(gsp_package, target_is_directory=True)
    # Restore the unqualified LVGL component alias normally supplied by the
    # component manager, while using only the already cached package.
    (components / 'lvgl').symlink_to(managed / 'lvgl__lvgl', target_is_directory=True)
    # hw_init's manifest normally supplies public touch dependencies. Restore
    # those relationships for offline builds; do not edit the original helper.
    touch = managed / 'espressif__esp_lcd_touch'
    if not touch.exists():
        parser.error('local cached touch base missing')
    (components / 'esp_lcd_touch').symlink_to(touch, target_is_directory=True)
    hw = components / 'hw_init'
    shutil.copytree(EXAMPLES / 'common/hw_init', hw)
    hw_cmake = hw / 'CMakeLists.txt'
    hw_text = hw_cmake.read_text()
    anchor = 'set(component_requires driver;esp_lcd)'
    if hw_text.count(anchor) != 1:
        parser.error('expected original hw_init requirements anchor missing')
    touch_drivers = ['espressif__esp_lcd_touch_gt911', 'espressif__esp_lcd_touch_gt1151',
                     'espressif__esp_lcd_touch_cst816s']
    if any(not (managed / name).exists() for name in touch_drivers):
        parser.error('cached touch driver missing; no dependencies downloaded')
    hw_cmake.write_text(hw_text.replace(anchor,
        'set(component_requires driver;esp_lcd;esp_lcd_touch;' + ';'.join(touch_drivers) + ')\n' +
        'if(IDF_TARGET STREQUAL \"esp32p4\")\n' +
        '    list(APPEND component_requires esp_lcd_ek79007)\n' +
        'else()\n' +
        '    list(APPEND component_requires espressif__esp_lcd_ili9341 espressif__esp_lcd_gc9a01 espressif__knob espressif__button)\n' +
        'endif()'))
    extra = [ROOT, ROOT / 'compat/esp_display_present', ROOT / 'compat/esp_lv_present',
             ROOT / 'bindings/esp_lv_present_v2', managed,
             ROOT / 'test_apps/boards/deps']
    cmake = ['cmake_minimum_required(VERSION 3.16)', 'set(ENV{IDF_COMPONENT_MANAGER} "0")',
             'set(EXTRA_COMPONENT_DIRS', *[' "' + str(p) + '"' for p in extra], ')',
             'set(COMPONENTS main' + (' espressif__esp-gsp espressif__esp_new_jpeg)' if gsp else ')'),
             'include($ENV{IDF_PATH}/tools/cmake/project.cmake)', 'project(' + args.example + ')']
    (output / 'CMakeLists.txt').write_text('\n'.join(cmake) + '\n')
    if gsp:
        # The original manifest supplies this dependency. With its resolver
        # disabled, restore build ordering explicitly, without changing C/C++.
        main_cmake = output / 'main/CMakeLists.txt'
        text = main_cmake.read_text()
        if 'PRIV_REQUIRES' not in text:
            parser.error('expected original main PRIV_REQUIRES anchor missing')
        text = text.replace('PRIV_REQUIRES esp-gsp ', 'PRIV_REQUIRES espressif__esp-gsp ')
        if 'PRIV_REQUIRES espressif__esp-gsp' not in text:
            text = text.replace('PRIV_REQUIRES', 'PRIV_REQUIRES espressif__esp-gsp', 1)
        main_cmake.write_text(text)
    files = {}
    for path in (source / 'main').rglob('*'):
        if path.is_file() and path.suffix in ('.c', '.h', '.cpp'):
            relative = str(path.relative_to(source))
            assert digest(path) == digest(output / relative)
            files[relative] = digest(path)
    manifest = {'example': args.example, 'source': str(source), 'business_sources_sha256': files,
                'managed_cache': str(managed), 'component_manager': 'disabled; local cache only',
                'scope': 'offline build; no board/runtime/benchmark acceptance implied'}
    if gsp:
        manifest['gsp_package'] = str(gsp_package)
        manifest['dependency_scope'] = 'Selected cached esp-gsp package; original version range is not resolved. Check package manifest before claiming release compatibility.'
        manifest['gsp_package_manifest'] = (gsp_package / 'idf_component.yml').read_text()
        manifest['gsp_archives_sha256'] = {
            str(path.relative_to(gsp_package)): digest(path)
            for path in sorted((gsp_package / 'prebuilt').glob('*/libesp-gsp.a'))}
    (output / 'legacy_source_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(output)


if __name__ == '__main__':
    main()
