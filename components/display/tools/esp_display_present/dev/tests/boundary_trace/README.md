# Host boundary-trace fixtures

These tests retain the bounded recorder's ordering/overflow/concurrency checks
and a legacy trace-format analyzer's negative fixtures. The `v2` scope string
in recorded/synthetic traces is a historical format tag, not a current backend.
Its two-callback model must not be used to infer the current RGB/MIPI retirement
protocol. Current backend behavior is tested by `test_scanout`, `test_fb` and the
public `rgb-handoff` board suite.

No test here modifies an IDF driver or inserts hooks into production sources.
The obsolete hardware overlay generator/capture programs are archived with the
[report tools](../../ARCHIVE.md). Run via `dev/tools/dev.py test` (and `--sanitize`).
