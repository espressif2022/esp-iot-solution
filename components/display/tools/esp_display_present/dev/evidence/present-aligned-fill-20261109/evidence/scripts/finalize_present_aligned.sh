#!/usr/bin/env bash
set -euo pipefail
TASK_OUT=/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-aligned-fill-20261109
python /tmp/report_present_aligned.py
python /tmp/analyze_present_aligned.py "$TASK_OUT"
python /tmp/archive_present_aligned.py
python /tmp/validate_present_aligned.py "$TASK_OUT"
PRESENT_ALIGNED_REPORT_ROOT="$TASK_OUT" node /tmp/check_present_aligned.mjs
python /tmp/publish_present_aligned.py
