import unittest
from analyze_trace import analyze


def case(events, dropped=0, ended=True):
    records = [(i, 100 + i * 10, kind, buf, detail, 0)
               for i, (kind, buf, detail) in enumerate(events)]
    return {'name': 'synthetic', 'scope': 'v2', 'count': len(records),
            'dropped': dropped, 'records': records, 'ended': ended}


class AnalysisTest(unittest.TestCase):
    def test_two_callbacks_and_retirement(self):
        result = analyze(case([
            (4, 1, 0), (2, 1, 0), (11, 2, 77), (12, 2, 77),
            (3, 1, 0), (4, 2, 0), (2, 2, 0), (21, 2, 1),
            (21, 2, 1), (13, 2, 77), (14, 1, 77),
            (22, 0, 0), (15, 1, 0), (16, 1, 0), (23, 0, 0), (19, 0, 0),
        ]))
        self.assertEqual(result['problems'], [])
        self.assertEqual(result['uncertainty'], [])
        self.assertEqual(result['callbacks_until_confirm']['min'], 2)
        self.assertEqual(result['last_read_end_to_release_us']['min'], 60)

    def test_unsafe_release_and_write(self):
        result = analyze(case([(4, 1, 0), (2, 1, 0), (14, 1, 0),
                               (15, 1, 0), (16, 1, 0), (19, 0, 0)]))
        for phrase in ('release selected', 'release while read active',
                       'write permission granted on selected', 'write permission granted while read active'):
            self.assertTrue(any(phrase in p for p in result['problems']), phrase)

    def test_incomplete_and_restart(self):
        result = analyze(case([(9, 0, 4), (19, 0, 0)], dropped=1))
        self.assertTrue(any('reason=0x4' in x for x in result['uncertainty']))
        self.assertIn('capture incomplete or dropped', result['uncertainty'])

    def test_unmatched_submission_not_silent(self):
        result = analyze(case([(11, 5, 7), (19, 0, 0)]))
        self.assertTrue(any('no matched subsequent selection' in x for x in result['uncertainty']))


if __name__ == '__main__':
    unittest.main()
