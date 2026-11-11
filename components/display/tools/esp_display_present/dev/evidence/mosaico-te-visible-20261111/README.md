# MOSAICO TE visible combinations — 2026-11-11

S31 MOSAICO CO5300 QSPI40MHz, TE43,480×480, MAC30:ed:a0:f4:0b:34, ACM1. Production39156930d; clean ESP-IDF7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe. No production changes or IDF patches.

Present TE combinations C1/C2×T1/T2×0/90/180/270°,16cases with16LVGL scenes each (256scenes). Frozen benchmark-only overlay displays implementation, mode, angle and case number in a fixed top layer. The overlay changes rendering workload, so these are visual-diagnostic results; they do not replace clean comparison baseline FPS/memory. See summary.csv and results-with-te.json.

Two initial serial captures interrupted before completing a case. Kernel logs showed the entire USB Hub disconnecting, including both CP210x devices and ACM1; this is not evidence of an application panic. User elected to keep the physical connection. Subsequent capture supports reconnecting to the fixed USB identity without resetting the MCU on reconnect; link interruptions are reported separately if present. Partial interrupted runs are preserved separately and excluded from scores.

Serial completion verifies scenes, buffer audit, memory records and successful close. It cannot by itself establish absence of dirty pixels or tearing. Top annotations allow user observations to be matched to a specific case. Original firmware was not restored, as requested.

Memory note: the first C1/T1/0° case retained500bytes of PSRAM after close; the subsequent15cases returned to their own before-open PSRAM values. This record does not attribute the one-time retention to a specific allocation without a heap trace.
