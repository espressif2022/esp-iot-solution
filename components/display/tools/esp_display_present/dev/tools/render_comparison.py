# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Render the comparison dashboard from recorded data, without rerunning tests."""
import argparse
import hashlib
import json
import os
from pathlib import Path

MODES = ['DOUBLE_FULL', 'TRIPLE_FULL', 'DOUBLE_DIRECT',
         'DOUBLE_PARTIAL', 'TRIPLE_PARTIAL', 'AUTO']
ANGLES = [0, 90, 180, 270]
BOARDS = {
    's3-rgb': ('S3 RGB', '480 × 480 · R16', '10 行 bounce · FB_IRAM 状态见测试来源'),
    's31-rgb': ('S31 Korvo RGB', '800 × 480', '非 bounce · AXI link-switch'),
    'p4-dsi': ('P4 MIPI DSI', '1024 × 600', 'DW GDMA'),
}


def render(data_path, output_path):
    raw = data_path.read_bytes()
    source = json.loads(raw)
    if not source.get('complete') or source.get('xip') != 'on' or source.get('angles') != ANGLES:
        raise ValueError('Expected a complete four-angle XIP ON dataset')
    payload = {
        'updated': source['updated'], 'sourceCommit': source['source_commit'],
        'dataSha256': hashlib.sha256(raw).hexdigest(), 'modes': MODES, 'angles': ANGLES,
        'boards': [], 'totals': {'clean': 0, 'diagnostic': 0, 'scenes': 0, 'skip': 0},
        'linksPrefix': os.path.relpath(data_path.parent, output_path.parent).replace(os.sep, '/') + '/',
        'dataFilename': data_path.name,
        'roundNote': source.get('round_note', '全部四角度重新完整运行，未用历史 0° 或 XIP OFF 成绩补表。'),
    }
    expected = {(mode, angle) for mode in MODES for angle in ANGLES}
    for key, (name, resolution, config) in BOARDS.items():
        original = source['boards'][key]
        board = {'id': key, 'name': name, 'resolution': resolution, 'config': original.get('config', config),
                 'method': source['methods'][key], 'cohorts': {},
                 'originNote': original.get('origin_note', ''),
                 'frameBytes': {'s3-rgb': 480 * 480 * 2, 's31-rgb': 800 * 480 * 2, 'p4-dsi': 1024 * 600 * 2}[key],
                 'memoryAccounting': source.get('memory_accounting', {}).get('boards', {}).get(key)}
        for cohort in ('clean', 'diagnostic'):
            board['cohorts'][cohort] = {}
            for implementation in ('present', 'adapter'):
                entries = original['cohorts'][cohort][implementation]['rows']
                if len(entries) != len(expected) or {(r['mode'], r['rotation']) for r in entries} != expected:
                    raise ValueError(f'Missing or duplicate cases: {key}/{cohort}/{implementation}')
                rows = []
                for entry in entries:
                    fields = ('mode', 'rotation', 'status', 'lvgl_fps', 'memory', 'scene_scores') if cohort == 'clean' else (
                        'mode', 'rotation', 'status', 'address', 'supply', 'address_events')
                    rows.append({field: entry[field] for field in fields if field in entry})
                    if entry['status'] == 'SKIP':
                        if implementation != 'adapter' or entry['mode'] != 'AUTO':
                            raise ValueError('Unexpected skipped case')
                        payload['totals']['skip'] += 1
                    elif entry['status'] == 'PASS' and entry['scenes'] == 16:
                        payload['totals'][cohort] += 1
                        payload['totals']['scenes'] += entry['scenes']
                    else:
                        raise ValueError('Unfinished case cannot be published')
                board['cohorts'][cohort][implementation] = rows
        payload['boards'].append(board)
    template = Path(__file__).with_name('comparison_dashboard.html').read_text()
    embedded = json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(',', ':')).replace('<', '\\u003c')
    page = template.replace('__REPORT_DATA__', embedded)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(page)
    return {'output': str(output_path), 'data_sha256': payload['dataSha256'], **payload['totals']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(render(args.data.resolve(), args.output.resolve()), ensure_ascii=False))


if __name__ == '__main__':
    main()
