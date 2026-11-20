# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Apply a checked, narrow refresh-order integration without editing LVGL."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()
check = """    lv_draw_buf_t * buf_act = disp_refr->buf_act;
    if(!(buf_act && buf_act->data && buf_act->data_size)) {
        LV_LOG_WARN("No draw buffer");
        LV_PROFILER_REFR_END;
        return;
    }

"""
entries = re.findall(r'void lv_display_refr_timer\(lv_timer_t \* (?:tmr|timer)\)\n\{', source)
if len(entries) != 1:
    sys.exit('Unsupported LVGL refresh entry signature')
entry = entries[0]
anchor = '    lv_refr_join_area();\n    refr_sync_areas();\n    refr_invalid_areas();'
for fragment in (check, entry, anchor):
    if source.count(fragment) != 1:
        sys.exit('Unsupported LVGL refresh source layout; review the integration for this LVGL version')
source = source.replace(check, '', 1)
source = source.replace(entry, """/* Optional binding hook; displays without a Present binding retain their path. */
extern bool esp_lv_present_prepare_refresh(lv_display_t *, bool) __attribute__((weak));

""" + entry, 1)
source = source.replace(anchor, """    /* Layout may invalidate objects. Borrow only once damage is known, before
     * any DIRECT synchronization or drawing can touch a framebuffer. */
    if(esp_lv_present_prepare_refresh &&
       !esp_lv_present_prepare_refresh(disp_refr, disp_refr->inv_p != 0)) goto refr_finish;

""" + check.replace('        LV_PROFILER_REFR_END;\n        return;', '        goto refr_finish;') + anchor, 1)
output = pathlib.Path(sys.argv[2])
if not output.exists() or output.read_text() != source:
    output.write_text(source)
