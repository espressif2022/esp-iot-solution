# Driver-only RGB event probe

`rgb-events` runs raw RGB driver submissions on the S31 Korvo board. It records
frame-complete/VSYNC behavior, callback counts and submit/wait timing under its
own driver configuration; it does not instantiate the present engine.

Use `dev/tools/run.py build --board s31-rgb --suite rgb-events` after the normal
prepare step. Builds/Flash backups remain in the external work root. Hardware
operations require selecting and verifying the board and serial port separately.

This probe characterizes this SDK/SoC/driver configuration. A callback count is
not proof of source retirement and says nothing by itself about presenter leases.
For the actual public presenter and early-completion/cross-core/reopen regression,
use [rgb-handoff](../rgb_handoff/README.md). No old non-bounce rejection rule is
imposed by this driver-only program.
