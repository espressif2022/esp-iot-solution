from pathlib import Path
import shutil,json,hashlib,re
r=Path('/tmp/present-xip-on-comparison-20261110');o=r/'driver-overlay';shutil.copytree('/tmp/present-s3-xip-on-address-20261110/driver-overlay',o)
c=Path('/tmp/present-xip-on-diag-source-20261110/components/display/tools/esp_display_present');shutil.copytree('/tmp/present-s3-on-address-source-20261110/components/display/tools/esp_display_present',c,symlinks=True,ignore=shutil.ignore_patterns('evidence','build','build-*','__pycache__','managed_components'))
a=c.parent/'esp_lvgl_adapter';shutil.copytree('/tmp/present-s3-on-address-source-20261110/components/display/tools/esp_lvgl_adapter',a,symlinks=True)
(c.parent/'esp_lv_present').symlink_to('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_lv_present',target_is_directory=True)
h=o/'esp_lcd/include/esp_lcd_present_trace.h';s=h.read_text().replace('uint32_t measured_frames,','uint32_t run_boundaries, underruns;\n    uint32_t measured_frames,').replace('unsigned *position);','unsigned *position, uintptr_t *raw);');s+='\nuintptr_t esp_lcd_present_trace_dpi_source(void *panel, uintptr_t *target, unsigned *position, uintptr_t *raw);\nvoid esp_lcd_present_trace_boundary(void *panel);\nvoid esp_lcd_present_trace_underrun(void *panel);\n';h.write_text(s)
p=o/'esp_lcd/src/esp_lcd_present_trace.c';s=p.read_text();s=s.replace('#include "esp_attr.h"','#include "esp_attr.h"\n#include "soc/soc_caps.h"')
s=s.replace('''        if (trace.view.measuring == 1) {
            trace.view.measuring = 2; /* Discard the partial frame at RUN entry. */
        } else if (trace.view.measuring == 2) {''','''        if (trace.view.measuring == 2) {''')
s=s.replace('uintptr_t given,source,target;unsigned kind,position,request_seq,event_seq;', 'uintptr_t given,source,target,raw;unsigned kind,position,request_seq,event_seq;')
s=s.replace('trace.view.measuring==2 && trace.view.kind==1 && trace.view.bounce_bytes && trace.view.panel','trace.view.measuring==2 && trace.view.panel && (trace.view.kind==1 || trace.view.kind==4)')
s=s.replace('''            uintptr_t target;unsigned position;
            uintptr_t source=esp_lcd_present_trace_rgb_source((void*)trace.view.panel,&target,&position);''','''            uintptr_t target=0, raw=0, source=0;unsigned position=0;
            if(trace.view.kind==1) source=esp_lcd_present_trace_rgb_source((void*)trace.view.panel,&target,&position,&raw);
#if defined(PRESENT_TRACE_HOST) || SOC_MIPI_DSI_SUPPORTED
            if(trace.view.kind==4) source=esp_lcd_present_trace_dpi_source((void*)trace.view.panel,&target,&position,&raw);
#endif''')
s=s.replace('audit.samples[i].position=position;', 'audit.samples[i].position=position;audit.samples[i].raw=raw;')
s=s.replace('confirm_wrong=%u saved=%u\\n",mode,rotation,','confirm_wrong=%u saved=%u boundaries=%u underruns=%u\\n",mode,rotation,')
s=s.replace('audit.confirm_wrong,audit.saved);','audit.confirm_wrong,audit.saved,(unsigned)trace.view.run_boundaries,(unsigned)trace.view.underruns);')
s=s.replace('position_px=%u request_seq=%u event_seq=%u\\n','position=%u request_seq=%u event_seq=%u probe_raw=0x%"PRIxPTR"\\n')
s=s.replace('audit.samples[i].request_seq,audit.samples[i].event_seq);','audit.samples[i].request_seq,audit.samples[i].event_seq,audit.samples[i].raw);')
s+='''
void TRACE_IRAM esp_lcd_present_trace_boundary(void *panel)
{
    ENTER();
    if((uintptr_t)panel==trace.view.panel){
        if(trace.view.measuring==1)trace.view.measuring=2;
        else if(trace.view.measuring==2)++trace.view.run_boundaries;
    }
    EXIT();
}
void TRACE_IRAM esp_lcd_present_trace_underrun(void *panel)
{
    ENTER();if((uintptr_t)panel==trace.view.panel && trace.view.measuring==2)++trace.view.underruns;EXIT();
}
''';p.write_text(s)
p=o/'esp_lcd/rgb/esp_lcd_panel_rgb.c';s=p.read_text();s=s.replace('#include "hal/axi_dma_ll.h"','#include "hal/axi_dma_ll.h"\n#include "hal/gdma_ll.h"')
s=s.replace('    uint8_t cur_fb_index;','    int trace_dma_group, trace_dma_channel; /* Diagnostic only. */\n    uint8_t cur_fb_index;',1)
s=s.replace('    esp_lcd_present_trace_bind(&rgb_panel->base,','    gdma_get_group_channel_id(rgb_panel->dma_chan, &rgb_panel->trace_dma_group, &rgb_panel->trace_dma_channel);\n    esp_lcd_present_trace_bind(&rgb_panel->base,',1)
s=s.replace('group == 0) {\n        raw = axi_dma_ll_tx_get_prefetched_desc_addr(AXI_DMA_LL_GET_HW(group), channel);','group == GDMA_LL_AXI_GROUP_START_ID) {\n        raw = axi_dma_ll_tx_get_prefetched_desc_addr(AXI_DMA_LL_GET_HW(group - GDMA_LL_AXI_GROUP_START_ID), channel);')
s=s.replace('        ESP_EARLY_LOGE(TAG, "LCD underrun");','        esp_lcd_present_trace_underrun(&rgb_panel->base);\n        ESP_EARLY_LOGE(TAG, "LCD underrun");')
needle='''        lcd_rgb_panel_try_update_pclk(rgb_panel);

#if RGB_LCD_NEEDS_SEPARATE_RESTART_LINK'''
assert needle in s
# Gate after S3 supply sample to exclude the first partial period.
needle='''            lcd_rgb_panel_try_restart_transmission(rgb_panel);
        }
#endif

    }''';assert needle in s;s=s.replace(needle,'''            lcd_rgb_panel_try_restart_transmission(rgb_panel);
        }
#endif
        esp_lcd_present_trace_boundary(&rgb_panel->base);

    }''')
start=s.index('uintptr_t IRAM_ATTR esp_lcd_present_trace_rgb_source(')
s=s[:start]+'''
uintptr_t IRAM_ATTR esp_lcd_present_trace_rgb_source(void *panel, uintptr_t *target, unsigned *position, uintptr_t *raw)
{
    esp_rgb_panel_t *rgb=__containerof((esp_lcd_panel_t*)panel,esp_rgb_panel_t,base);
    *target=(uintptr_t)rgb->fbs[rgb->cur_fb_index];*position=rgb->bounce_pos_px;*raw=0;
    if(rgb->bb_size){
        volatile uint8_t *source_index=&rgb->bb_fb_index;
        unsigned first=*source_index;unsigned last=*source_index;
        return first==last && first<rgb->num_fbs ? (uintptr_t)rgb->fbs[first] : 0;
    }
#if RGB_LCD_USE_GDMA_LINK_SWITCH_EVENT
    int group=rgb->trace_dma_group-GDMA_LL_AXI_GROUP_START_ID,channel=rgb->trace_dma_channel;
    if(group!=0)return 0;
    uintptr_t first=axi_dma_ll_tx_get_prefetched_desc_addr(AXI_DMA_LL_GET_HW(group),channel);
    uintptr_t last=axi_dma_ll_tx_get_prefetched_desc_addr(AXI_DMA_LL_GET_HW(group),channel);*raw=last;
    for(unsigned i=0;i<rgb->num_fbs;i++){
        uintptr_t head=gdma_link_get_head_addr(rgb->dma_fb_links[i]),end=gdma_link_get_item_addr(rgb->dma_fb_links[i],-1);
        if(first>=head && first<=end && last>=head && last<=end){*position=(unsigned)(last-head);return (uintptr_t)rgb->fbs[i];}
    }
#endif
    return 0;
}
''';p.write_text(s)
p=o/'esp_lcd/dsi/esp_lcd_panel_dpi.c';s=p.read_text();s=s.replace('#include "esp_lcd_present_trace.h"','#include "esp_lcd_present_trace.h"\n#include "hal/dw_gdma_ll.h"')
s=s.replace('    uint8_t cur_fb_index;','    volatile uint8_t trace_latched_index; /* Real DMA-programming snapshot, not producer target. */\n    int trace_dma_channel;\n    uint8_t cur_fb_index;',1)
needle='    dw_gdma_channel_enable_ctrl(chan, true);';assert needle in s;s=s.replace(needle,'    dpi_panel->trace_latched_index = fb_index;\n'+needle,1)
needle='    esp_lcd_present_trace_event(&dpi_panel->base, 4, dpi_panel->cur_fb_index, fb_index, (uintptr_t)link_list);';assert needle in s;s=s.replace(needle,needle+'\n    esp_lcd_present_trace_boundary(&dpi_panel->base);')
s=s.replace('        ESP_DRAM_LOGE(TAG, "can\'t fetch data','        esp_lcd_present_trace_underrun(&dpi_panel->base);\n        ESP_DRAM_LOGE(TAG, "can\'t fetch data')
s=s.replace('    esp_lcd_present_trace_bind(&dpi_panel->base,','    dw_gdma_channel_get_id(dpi_panel->dma_chan, &dpi_panel->trace_dma_channel);\n    esp_lcd_present_trace_bind(&dpi_panel->base,',1)
s+='''
uintptr_t IRAM_ATTR esp_lcd_present_trace_dpi_source(void *panel, uintptr_t *target, unsigned *position, uintptr_t *raw)
{
    esp_lcd_dpi_panel_t *dpi=__containerof((esp_lcd_panel_t*)panel,esp_lcd_dpi_panel_t,base);
    unsigned first=dpi->trace_latched_index;
    *target=(uintptr_t)dpi->fbs[dpi->cur_fb_index];*position=0;
    *raw=dw_gdma_ll_channel_get_current_link_list_item_addr(DW_GDMA_LL_GET_HW(0),dpi->trace_dma_channel);
    unsigned last=dpi->trace_latched_index;
    return first==last && first<dpi->num_fbs ? (uintptr_t)dpi->fbs[first] : 0;
}
''';p.write_text(s)
# No identity overlay in this rerun: common benchmark source has only original UI.
p=c/'dev/test_apps/benchmark/main/main.c';s=p.read_text();start=s.index('        lv_obj_t *identity =');end=s.index('        bench_unlock();',start);s=s[:start]+s[end:];p.write_text(s)
m=json.loads((o/'trace-manifest.json').read_text());m['files_sha256']={str(p.relative_to(o)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((o/'esp_lcd').rglob('*')) if p.is_file()};m['address_audit']='S3: live bounce-source index; S31: live AXI prefetch descriptor mapped to FB, global-to-local group conversion; P4: independent snapshot of descriptor FB actually programmed at DMA restart, LLP context. No control changes.';m['measurement_gate']='RGB VSYNC boundaries (after S3 supply sample), DSI frame DMA completion; discard leading partial period';(o/'trace-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
(r/'diagnostic-source.json').write_text(json.dumps({'path':str(c),'methods':{'s3-rgb':'actual bounce PSRAM source','s31-rgb':'AXI prefetched descriptor FB; conservative DMA borrow check, not pixel scan position','p4-dsi':'driver programmed DMA frame snapshot; LLP context, not physical pixel scan proof'}},indent=2)+'\n')
print(c)
