# Component regression tests

This directory contains Host regressions of production code. The target smoke
application lives in `../../test_apps/smoke`; `../run.py` is the shared entry.
Benchmark datasets, lab device identities and board diagnostics are local-only
under `../../dev` and are not required by formal tests or distributed packages.

## Host

CMake, a C/C++ compiler, Threads and Python 3 are required. From the component root:

```sh
python3 tests/run.py test
python3 tests/run.py test --sanitize
python3 tests/run.py test --lvgl-dir /path/to/lvgl
python3 tests/run.py test --lvgl-dir /path/to/lvgl --sanitize
```

LVGL tests require an existing LVGL 9 source checkout; this runner never downloads
it. Output defaults to `/tmp/esp-display-present-tests`; `--build-dir` overrides it.
Tests cover buffer ownership, late callbacks, rollback, timeouts, rotation,
planning, hardware API variants and the LVGL binding. Use the actual CTest count
for the selected configuration. Host tests do not prove optical tear freedom.

The handoff regressions compile the real recovery helper and caller from
`examples/display/gui/gsp_lvgl_present_handoff`, using only platform mocks.
Keep that example available when exporting the Host test tree. Its source is
also covered by the Host CI change rules.

An optional integration test accepts `--gsp-dir /path/to/gsp` together with
`--lvgl-dir`. It requires the GSP split `components/esp_gsp_adapter`,
`cmake/gsp_portable_targets.cmake`, the ESP-IDF runtime sources and the generated
Host fixture `ci/host/data/out_nav/nav.gspb`. No GSP source or bundle is vendored.

## ESP-IDF smoke

Activate ESP-IDF 6.0 or newer, then run:

```sh
python3 tests/run.py idf-build --target esp32s3
python3 tests/run.py idf-build --target esp32p4
```

This application verifies target API and link integration using a loopback
fixture. It is not a panel benchmark and does not access a real display.
DSI scanout requires SDK callbacks newer than those available in ESP-IDF 6.0;
a successful P4 smoke build does not establish DSI runtime support.
