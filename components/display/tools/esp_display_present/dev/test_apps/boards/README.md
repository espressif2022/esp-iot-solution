# Pixel and mode board tests

This application exercises the current public presenter, native framebuffers and
board adapters without LVGL. Reusable sources remain under `main/`; historical
stage reports and fixed serial-port commands are not current acceptance criteria.

Use the component's runner and [board definitions](../../boards/boards.json):

```sh
python3 dev/tools/run.py prepare --work-root /tmp/present-boards
python3 dev/tools/run.py build --board s3-rgb --suite pixels --work-root /tmp/present-boards
python3 dev/tools/run.py build --board p4-dsi --suite native --work-root /tmp/present-boards
```

Run these from the component root after activating a matching clean IDF. The
runner prepares cached dependencies externally and supplies the current defaults
from `dev/boards/configs`. Use `--help` for explicit backup/flash/capture actions;
port names are examples and board identity must be verified before flashing.
This documentation update does not authorize a hardware run.

`pixels` selects the public mode/rotation matrix; `native` selects the separate
native framebuffer matrix. They are mutually exclusive. Some additional direct
CMake options are diagnostics, not default passing tests. In particular the old
P4 cross-axis probe is not evidence of a current known failure; actual acceptance
must compare returned errors, physical bounds and pixels against current policy.

S3 RGB uses ten-line bounce; S31 Korvo uses its BSP creation facts and non-bounce
link-switch protocol; P4 uses DPI scanout. There is no universal two-VSYNC fence.
The adapter selects its protocol internally. See [mode/ownership rules](../../../docs/MODE_ARCHITECTURE.md).

Host checks do not establish optical correctness. Pixel memory, receipt success
and event order cannot alone prove that the visible display is tear-free.
For early callbacks, cross-core submissions and stop/reopen use
[RGB handoff](../rgb_handoff/README.md); for LVGL throughput use `benchmark`.
Historical data and reproduction boundaries are in [VALIDATION.md](../../VALIDATION.md).
