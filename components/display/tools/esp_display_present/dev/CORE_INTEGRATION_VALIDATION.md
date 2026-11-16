# Core integration validation — 2026-11-16

## Scope

Base: `1bb3ad36e`. Selected changes from PR head `569dbdf46ed2b8d12c52287bc7a258a0f0cd2557`: buffer lifetime and timeout safety, handoff recovery, DMA2D/PPA executor support, lazy resources/shared worker, and regression tests/metrics. The LVGL PPA fill wrapper and driver cache-writeback bypass are excluded.

Local corrections retain a pause token on error and resume it without entering LVGL; peak FB metadata and stack budgets cover later rotation despite lazy initial allocation. The capture runner now distinguishes `EXECUTOR_SUITE_PASS` from actual matrix completion.

## Checks

- Host ordinary, without LVGL: 52/52 passed.
- Host ASan/UBSan with LVGL 9.5: 56/56 passed.
- P4 executor: 32 cases passed (20 hardware, 12 CPU fallback), including isolated DMA2D/PPA client destruction.
- P4 pixels: `MATRIX_PASS`, 24 cases, four rotations, four frames per case.
- Four-board benchmark: 14 complete cases per revision, each with all 16 LVGL scenes (28 cases total). These are representative cases, not a new exhaustive all-mode/all-angle matrix.

SDK: clean ESP-IDF `7b9cc1ac79f`; LVGL 9.5.0. Each board used matching baseline/new sdkconfig fingerprints. RGB/DSI used XIP on; S3 RGB used bounce buffers, S31 RGB used non-bounce. BOX-3 retained its matching baseline configuration.

## Measured A/B

FPS is measured transport FPS. Internal heap cost is `before_open.free - after_open.free`, in KiB; smaller is better. These are single complete benchmark runs, not confidence intervals. The new benchmark includes opt-in metrics; its memory overhead is included.

| Board | Mode | Rotation | FPS base → new | Internal opening cost base → new (KiB) | New supply misses |
|---|---|---:|---:|---:|---:|
| s3-rgb | DOUBLE_FULL | 0° | 21.9 → 21.8 | 43.79 → 40.33 | 0 |
| s3-rgb | TRIPLE_PARTIAL | 0° | 30.8 → 30.4 | 69.17 → 66.16 | 13 |
| s31-rgb | DOUBLE_FULL | 0° | 29.1 → 29.1 | 44.96 → 40.39 | 0 |
| s31-rgb | TRIPLE_PARTIAL | 0° | 31.9 → 32.0 | 101.41 → 98.34 | 0 |
| p4-dsi | DOUBLE_FULL | 0° | 21.7 → 21.7 | 45.28 → 40.53 | 0 |
| p4-dsi | DOUBLE_FULL | 90° | 15.9 → 15.8 | 23.42 → 20.07 | 0 |
| p4-dsi | DOUBLE_DIRECT | 0° | 28.4 → 28.8 | 23.42 → 20.23 | 0 |
| p4-dsi | DOUBLE_DIRECT | 90° | 19.9 → 20.2 | 23.40 → 19.88 | 201 |
| p4-dsi | TRIPLE_PARTIAL | 0° | 38.7 → 39.1 | 123.57 → 120.35 | 0 |
| p4-dsi | TRIPLE_PARTIAL | 90° | 35.0 → 34.8 | 82.20 → 78.70 | 4 |
| box3-spi | GRAM_T1_INTERNAL | 0° | 30.4 → 30.4 | 60.11 → 61.36 | — |
| box3-spi | GRAM_T1_INTERNAL | 90° | 31.2 → 31.2 | 62.51 → 63.75 | — |
| box3-spi | GRAM_T2_PSRAM | 0° | 35.8 → 35.8 | 78.52 → 79.79 | — |
| box3-spi | GRAM_T2_PSRAM | 90° | 33.4 → 33.3 | 62.89 → 64.16 | — |

The sampled scanout supply-miss counters are not buffer-ownership violation counts. Nonzero values remain in S3 TRIPLE_PARTIAL and P4 rotated DIRECT/PARTIAL; this integration does not claim to eliminate underflow or prove visually tear-free output. Passing serial/pixel checks alone is insufficient for that claim.

## Memory interpretation

- Allocated static DRAM data/BSS did not change in the four benchmark ELFs. Rounded IRAM section footprint grew by 256 B on S3, S31 and BOX-3, and 384 B on P4.
- XIP text/rodata growth also reduces available PSRAM. A lower PSRAM total is not evidence of another framebuffer.
- S31 first-run closing internal-free values shifted by 112 B between cases. A separate three-run TRIPLE_PARTIAL repetition closed at exactly 387191 B internal and 13533708 B PSRAM free each time. No progressive loss was observed in that repetition; the owner of the first-run retained difference is not established.

## Limits and evidence

MOSAICO TE is pending: the USB-OTG-connected unit was backed up and flashed, but its console did not enumerate on that connector; testing was stopped at the user’s request. A different known MOSAICO later enumerated as ACM0; it was only inspected, not flashed or tested in this run. No TE performance or visual result is claimed here.

Raw logs, per-case heap snapshots, configuration manifests and the isolated S31 repetition are retained outside the source tree under `/tmp/present-core-20261116/`. Build directories, ELF images and full flash backups are deliberately not committed. The historical tables in `VALIDATION.md` are separate measurements and must not be substituted for this subset.
