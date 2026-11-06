# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import subprocess

spec = importlib.util.spec_from_file_location('present_run', Path(__file__).parents[1] / 'tools/run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class RunnerTest(unittest.TestCase):
    def test_board_matrix(self):
        self.assertEqual(len(runner.cases_for('s3-rgb', [0, 90, 180, 270])), 28)
        self.assertEqual(len(runner.cases_for('box3-spi', [0, 90, 180, 270])), 20)
        self.assertEqual(len(runner.cases_for('s31-qspi', [0, 90, 180, 270])), 16)

    def test_adapter_none_rotation_and_auto_not_faked(self):
        a = runner.cases_for('s3-rgb', [0, 90])
        self.assertFalse(next(c for c in a if c['name'] == 'NONE' and c['rotation'] == 90)['adapter_supported'])
        self.assertTrue(next(c for c in a if c['name'] == 'DOUBLE_DIRECT' and c['rotation'] == 90)['adapter_supported'])
        self.assertTrue(all(not c['adapter_supported'] for c in a if c['name'] == 'AUTO'))

    def test_explicit_profile_filter(self):
        self.assertEqual(len(runner.cases_for('s31-qspi', [0, 90], ['TE_C2_T2'])), 2)
        with self.assertRaises(ValueError):
            runner.cases_for('box3-spi', [0], ['TE_C2_T2'])

    def test_gate_and_render_metrics_separate(self):
        log = 'RUN mode=TE_C2_T2 rotation=90\nTE_METER: admitted=5 wait_avg_us=1700 tx_avg_us=24000\nPASS mode=TE_C2_T2 rotation=90 frames=5 lvgl_fps=30 scenes=16\nSUITE_PASS board=x\n'
        value = runner.parse_log(log)
        self.assertTrue(value['complete'])
        self.assertEqual(value['rows'][0]['wait_avg_us'], 1700)
        self.assertEqual(value['rows'][0]['tx_avg_us'], 24000)

    def test_error_is_not_pass(self):
        self.assertTrue(runner.parse_log('E (14) worker: error\nSUITE_PASS board=x')['errors'])
        self.assertFalse(runner.parse_log('E (14) gpio: GPIO isr service already installed')['errors'])
        self.assertFalse(runner.parse_log('PASS mode=NONE rotation=0 lvgl_fps=1')['complete'])

    def test_suite_markers(self):
        for marker in ['MATRIX_PASS board=x', 'TE_SUITE_PASS normal_cases=8', 'present_v2 smoke passed']:
            self.assertTrue(runner.parse_log(marker)['complete'])

    def test_wrong_mac_stops_before_write(self):
        args = SimpleNamespace(port='/dev/example', board='s3-rgb')
        with patch.object(runner.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, stdout='MAC: 00:00:00:00:00:00')) as run:
            with self.assertRaises(ValueError):
                runner.identify(args)
            self.assertEqual(run.call_count, 1)
            self.assertIn('read-mac', run.call_args.args[0])

    def test_compare_and_configuration_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = SimpleNamespace(work_root=Path(tmp), board='s3-rgb')
            base = Path(tmp) / args.board / 'benchmark'
            cases = runner.cases_for('s3-rgb', [90], ['NONE'])
            manifest = dict(board='s3-rgb', cases=cases, idf_commit='abc', lvgl_benchmark_sha256='def', comparable_sdkconfig_sha256='123')
            for impl in ['present', 'adapter']:
                root = base / impl
                root.mkdir(parents=True)
                (root / 'manifest.json').write_text(json.dumps(manifest))
                row = dict(mode='NONE', rotation=90, status='SKIP' if impl == 'adapter' else 'PASS', lvgl_fps=20)
                (root / 'results.json').write_text(json.dumps(dict(complete=True, matrix_complete=True, errors=[], rows=[row])))
            runner.compare(args)
            self.assertIn(',20,-', (base / 'comparison.csv').read_text())
            for key in ['board', 'idf_commit', 'lvgl_benchmark_sha256', 'comparable_sdkconfig_sha256']:
                different = dict(manifest)
                different[key] = 'different'
                (base / 'adapter/manifest.json').write_text(json.dumps(different))
                with self.assertRaises(ValueError):
                    runner.compare(args)

    def test_partial_report_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = SimpleNamespace(work_root=Path(tmp), board='s3-rgb', suite='benchmark', implementation='present')
            root = runner.location(args)
            root.mkdir(parents=True)
            (root / 'manifest.json').write_text(json.dumps(dict(cases=runner.cases_for('s3-rgb', [0], ['NONE']))))
            (root / 'live.log').write_text('SUITE_PASS board=x')
            with self.assertRaises(ValueError):
                runner.report(args)


if __name__ == '__main__':
    unittest.main()
