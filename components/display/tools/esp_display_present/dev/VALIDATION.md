# Validation summary

This file summarizes evidence; it is not the complete measurement dataset.
The 2026-11-11 board results retain their tested revisions/configuration reuse.
They are not a fresh hardware run of this MR's later cleanup commits.

## Code checks

| Check | Result |
|---|---|
| Host / ASan+UBSan | 44/44 / 44/44 |
| LVGL 9.5 binding / ASan+UBSan | 48/48 / 48/48 |
| Clean-branch Host and repository hooks | 44/44; hooks passed |
| P4 / S31 smoke, local IDF `80c8922480b5` | Compiled, not flashed |
| Exact IDF v6.0 S3 / P4 smoke | Compiled, not flashed; P4 uses CPU fallback, DSI adapter unavailable on this SDK |
| Public P4 `lvgl_present_benchmark`, IDF v6.0 | Dependency resolution and build passed in isolated copy; not flashed, no DSI runtime support claim |
| P4 smoke, local IDF `80c8922480b5`, after SDK API detection | Compiled with both accelerators and DPI callback support enabled; not flashed |

The minimum SDK compile check uses exact IDF v6.0 sources/submodule revisions,
not an arbitrary local 6.x development checkout. See [compatibility](../COMPATIBILITY.md):
CPU/core availability does not imply optional accelerator or DSI adapter availability.
Host and smoke checks do not establish optical tear freedom.

GitLab CI runs Host tests with and without ASan/UBSan, plus the LVGL 9.5.0 binding
in both variants. LVGL is pinned to commit
`85aa60d18b3d5e5588d7b247abf90198f07c8a63`. Smoke build jobs use the maintained
`dev/test_apps/smoke` entry on S3/P4, with IDF 6.0/6.1/latest. CI configuration
validation and local checks are not a claim that remote jobs have passed.

## Recorded hardware coverage

Each passing case completed 16 LVGL scenes and closed successfully. Angles:
0/90/180/270 degrees; creation rotation is distinct from runtime rotation.

| Board | Performance present / adapter | Address diagnostics present / adapter |
|---|---:|---:|
| S3 RGB 480x480 | 24 / 20 | 24 / 20 |
| S31 Korvo RGB 800x480 | 24 / 20 | 24 / 20 |
| P4 DSI 1024x600 | 24 / 20 | 24 / 20 |
| S31 MOSAICO QSPI TE 480x480 | 16 / 9 | Not collected |
| BOX-3 SPI 320x240 | 20 / 4 | Not collected |

Total: 181 performance cases, 132 diagnostics, 5008 scenes. Scanout uses XIP ON
and excludes NONE; S3 has 10-line bounce, S31 RGB has no bounce. TE native
configurations are listed separately; SPI GRAM permits tearing.

## Findings that remain relevant

- Address hits (release / write / confirmation): present 0/0/0 on each scanout
  board; adapter S3 0/2/2 across 2 cases, S31 0/0/0, P4 1/4/1 across 5 cases.
  Categories can overlap. Independent input buffers outside the scanout pool
  are not automatically errors; uninstrumented paths are not counted as zero.
- S3 matched 20 cases: present 163 short-supply frames / 83830 measured scans;
  adapter 96 / 83827. Existing scan/DMA restarts were respectively 163 and 96,
  not MCU resets. Zero address hits do not resolve this supply/visual risk.
- Memory reports four remaining-heap points (before/open/minimum/close) and
  actual ELF static SRAM separately. Whole-image costs are not component-only
  costs. Remaining heap is better when higher; static use is better when lower.
- Old PPA stalls and USB interruptions remain historical evidence, not current
  passing cases. Completed diagnostics do not replace optical validation.

## Download and reproduce

[Full five-board report, JSON/CSV, diagnostic details and history index](https://gitlab.espressif.cn:6688/-/project/274/uploads/3d4d3f108b6592ebdddc269ffa0ab5b4/present-five-board-results-20261111.tar.gz)
(352656 bytes; SHA-256 `27af070a4fadfd2a1ef90adb1c4456b6966485b43d951edf79fb18682796235f`).

Download [the matching report tools](https://gitlab.espressif.cn:6688/-/project/274/uploads/76639e147d43ba6eaa4aa187f650154a/present-report-tools-and-legacy-probe.tar.gz) and extract them to
`/path/to/tools`. SHA-256: `a918656f17e01d076c424c8802cf0560ba43bac21057cd27a13e5cc76c95dde4`.
The bundle also retains the old hardware overlay generator as historical source;
its old anchors/configuration are not claimed to work against current code.

```sh
python3 /path/to/tools/report-tools/build_latest_dashboard.py --snapshot /path/to/results/report/matrix-data.json --output /tmp/display-latest/matrix-data.json
python3 /path/to/tools/report-tools/render_comparison.py --data /tmp/display-latest/matrix-data.json --output /tmp/display-latest/comparison.html
```

The export requires the downloaded snapshot explicitly; it does not rely on an
in-repository historical dataset. Host tests need no report attachment.
See [development commands](README.md) and [raw evidence boundaries](ARCHIVE.md).
