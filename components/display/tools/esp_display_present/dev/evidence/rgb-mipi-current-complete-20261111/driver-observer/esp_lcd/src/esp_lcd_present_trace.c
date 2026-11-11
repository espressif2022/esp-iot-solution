/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "esp_lcd_present_trace.h"
#include <string.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#ifndef PRESENT_TRACE_HOST
#include "esp_attr.h"
#include "soc/soc_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define TRACE_IRAM IRAM_ATTR
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
#define ENTER() portENTER_CRITICAL_SAFE(&lock)
#define EXIT() portEXIT_CRITICAL_SAFE(&lock)
#define NOW() ((uint64_t)esp_timer_get_time())
#else
#define TRACE_IRAM
#define ENTER() ((void)0)
#define EXIT() ((void)0)
#define NOW() ((uint64_t)0)
#endif
#define SAVED_EVENTS 16
static struct {
    esp_lcd_present_trace_snapshot_t view;
    unsigned count;
    struct sample {
        uint64_t time_us;
        uint32_t request_seq, event_seq, kind;
        uintptr_t expected, observed, previous, raw;
    } samples[SAVED_EVENTS];
} trace;

void esp_lcd_present_trace_bind(void *panel, void *const *fbs, unsigned count, unsigned bounce_bytes, unsigned kind)
{
    ENTER();
    memset(&trace, 0, sizeof(trace));
    trace.view.panel = (uintptr_t)panel;
    trace.count = count > 3 ? 3 : count;
    for (unsigned i = 0; i < trace.count; ++i) {
        trace.view.fbs[i] = (uintptr_t)fbs[i];
    }
    trace.view.requested = trace.view.observed = trace.view.fbs[0];
    trace.view.bounce_bytes = bounce_bytes;
    trace.view.kind = kind;
    EXIT();
}

void TRACE_IRAM esp_lcd_present_trace_request(void *panel, unsigned index)
{
    ENTER();
    if ((uintptr_t)panel == trace.view.panel) {
        ++trace.view.requests;
        if (index >= trace.count) {
            ++trace.view.outside_pool;
        } else {
            if (trace.view.pending) {
                ++trace.view.superseded;
            }
            uintptr_t requested = trace.view.fbs[index];
            if (requested == trace.view.observed) {
                ++trace.view.active_submits;
                if (trace.view.saved < SAVED_EVENTS) {
                    trace.samples[trace.view.saved++] = (struct sample) {
                        NOW(), trace.view.requests, trace.view.events, 1, requested, trace.view.observed, trace.view.observed, 0
                    };
                }
            }
            trace.view.requested = requested;
            trace.view.pending = 1;
        }
    }
    EXIT();
}

void TRACE_IRAM esp_lcd_present_trace_event(void *panel, unsigned kind, unsigned expected, int observed, uintptr_t raw)
{
    uint64_t now = NOW();
    ENTER();
    if ((uintptr_t)panel == trace.view.panel) {
        ++trace.view.events;
        uintptr_t before = trace.view.observed;
        uintptr_t wanted = expected < trace.count ? trace.view.fbs[expected] : 0;
        uintptr_t actual = observed >= 0 && (unsigned)observed < trace.count ? trace.view.fbs[observed] : 0;
        bool suspicious = false;
        if (!actual) {
            ++trace.view.unresolved;
            suspicious = true;
        } else {
            if (wanted != actual) {
                ++trace.view.target_diffs;
                suspicious = true;
            }
            trace.view.observed = actual;
            if (actual == trace.view.requested) {
                trace.view.pending = 0;
            }
        }
        if (suspicious && trace.view.saved < SAVED_EVENTS) {
            trace.samples[trace.view.saved++] = (struct sample) {
                now, trace.view.requests, trace.view.events, kind, wanted, actual, before, raw
            };
        }
    }
    EXIT();
}

void TRACE_IRAM esp_lcd_present_trace_rgb_supply(void *panel, unsigned got, unsigned expected, unsigned restart)
{
    ENTER();
    if ((uintptr_t)panel == trace.view.panel) {
        if (!trace.view.supply_frames || got < trace.view.supply_min_eofs) trace.view.supply_min_eofs = got;
        ++trace.view.supply_frames;
        trace.view.supply_short += got < expected;
        trace.view.supply_restarts += restart != 0;
        trace.view.supply_expected = expected;
        if (trace.view.measuring == 2) {
            ++trace.view.measured_frames;
            trace.view.measured_short += got < expected;
            trace.view.measured_restarts += restart != 0;
        }
    }
    EXIT();
}

void esp_lcd_present_trace_mark_running(unsigned running)
{
    ENTER(); trace.view.measuring = running != 0; EXIT();
}

void esp_lcd_present_trace_reset(void)
{
    esp_lcd_present_trace_audit_reset();
    ENTER();
    esp_lcd_present_trace_snapshot_t old = trace.view;
    memset(&trace.view, 0, sizeof(trace.view));
    trace.view.panel = old.panel;
    for (unsigned i = 0; i < 3; ++i) {
        trace.view.fbs[i] = old.fbs[i];
    }
    trace.view.requested = old.requested;
    trace.view.observed = old.observed;
    trace.view.pending = old.pending;
    trace.view.bounce_bytes = old.bounce_bytes;
    trace.view.kind = old.kind;
    EXIT();
}
void esp_lcd_present_trace_snapshot(esp_lcd_present_trace_snapshot_t *out)
{
    ENTER(); *out = trace.view; EXIT();
}
void esp_lcd_present_trace_report(const char *mode, unsigned rotation)
{
    esp_lcd_present_trace_snapshot_t s;
    struct sample samples[SAVED_EVENTS];
    ENTER(); s = trace.view; memcpy(samples, trace.samples, sizeof(samples)); EXIT();
    printf("DRIVER_TRACE mode=%s rotation=%u kind=%u bounce_bytes=%u requests=%u events=%u superseded=%u active_submits=%u target_diffs=%u unresolved=%u outside_pool=%u pending=%u saved=%u fb0=0x%"PRIxPTR" fb1=0x%"PRIxPTR" fb2=0x%"PRIxPTR" requested=0x%"PRIxPTR" observed=0x%"PRIxPTR"\n", mode, rotation, (unsigned)s.kind, (unsigned)s.bounce_bytes, (unsigned)s.requests, (unsigned)s.events, (unsigned)s.superseded, (unsigned)s.active_submits, (unsigned)s.target_diffs, (unsigned)s.unresolved, (unsigned)s.outside_pool, (unsigned)s.pending, (unsigned)s.saved, s.fbs[0], s.fbs[1], s.fbs[2], s.requested, s.observed);
    printf("RGB_SUPPLY mode=%s rotation=%u frames=%u short_frames=%u restarts=%u min_eofs=%u expected_eofs=%u measured_frames=%u measured_short=%u measured_restarts=%u\n", mode, rotation, (unsigned)s.supply_frames, (unsigned)s.supply_short, (unsigned)s.supply_restarts, (unsigned)s.supply_min_eofs, (unsigned)s.supply_expected, (unsigned)s.measured_frames, (unsigned)s.measured_short, (unsigned)s.measured_restarts);
    esp_lcd_present_trace_audit_report(mode, rotation);
    for (unsigned i = 0; i < s.saved; ++i) {
        struct sample *p = &samples[i];
        printf("DRIVER_TRACE_EVENT mode=%s rotation=%u us=%"PRIu64" request_seq=%u event_seq=%u kind=%u expected=0x%"PRIxPTR" observed=0x%"PRIxPTR" previous=0x%"PRIxPTR" raw=0x%"PRIxPTR"\n", mode, rotation, p->time_us, (unsigned)p->request_seq, (unsigned)p->event_seq, (unsigned)p->kind, p->expected, p->observed, p->previous, p->raw);
    }
}

#define AUDIT_SAVED 16
static struct {
    unsigned releases,writes,confirms,not_pool,raced,release_active,write_active,confirm_wrong,saved;
    struct {uint64_t us;uintptr_t given,source,target,raw;unsigned kind,position,request_seq,event_seq;} samples[AUDIT_SAVED];
} audit;
void esp_lcd_present_trace_audit_reset(void){ENTER();memset(&audit,0,sizeof(audit));EXIT();}
void TRACE_IRAM esp_lcd_present_trace_audit(const void *pixels,unsigned kind)
{
    ENTER();
    if(trace.view.measuring==2 && trace.view.panel && (trace.view.kind==1 || trace.view.kind==4)){
        unsigned index;
        for(index=0;index<trace.count;index++)if((uintptr_t)pixels==trace.view.fbs[index])break;
        if(index==trace.count)++audit.not_pool;
        else {
            uintptr_t target=0, raw=0, source=0;unsigned position=0;
            if(trace.view.kind==1) source=esp_lcd_present_trace_rgb_source((void*)trace.view.panel,&target,&position,&raw);
#if defined(PRESENT_TRACE_HOST) || SOC_MIPI_DSI_SUPPORTED
            if(trace.view.kind==4) source=esp_lcd_present_trace_dpi_source((void*)trace.view.panel,&target,&position,&raw);
#endif
            if(!source)++audit.raced;
            else {
                if(kind==1)++audit.releases;
                if(kind==2)++audit.writes;
                if(kind==3)++audit.confirms;
                bool bad=(kind==3)?((uintptr_t)pixels!=source):((uintptr_t)pixels==source);
                if(bad){
                    if(kind==1)++audit.release_active;
                    if(kind==2)++audit.write_active;
                    if(kind==3)++audit.confirm_wrong;
                    if(audit.saved<AUDIT_SAVED){unsigned i=audit.saved++;audit.samples[i].us=NOW();audit.samples[i].given=(uintptr_t)pixels;audit.samples[i].source=source;audit.samples[i].target=target;audit.samples[i].position=position;audit.samples[i].raw=raw;audit.samples[i].kind=kind;audit.samples[i].request_seq=trace.view.requests;audit.samples[i].event_seq=trace.view.events;}
                }
            }
        }
    }
    EXIT();
}
void esp_lcd_present_trace_audit_report(const char *mode,unsigned rotation)
{
    printf("RGB_ADDRESS mode=%s rotation=%u releases=%u writes=%u confirms=%u not_pool=%u raced=%u release_active=%u write_active=%u confirm_wrong=%u saved=%u boundaries=%u underruns=%u\n",mode,rotation,audit.releases,audit.writes,audit.confirms,audit.not_pool,audit.raced,audit.release_active,audit.write_active,audit.confirm_wrong,audit.saved,(unsigned)trace.view.run_boundaries,(unsigned)trace.view.underruns);
    for(unsigned i=0;i<audit.saved;i++)printf("RGB_ADDRESS_EVENT mode=%s rotation=%u us=%"PRIu64" kind=%u given=0x%"PRIxPTR" actual_source=0x%"PRIxPTR" driver_target=0x%"PRIxPTR" position=%u request_seq=%u event_seq=%u probe_raw=0x%"PRIxPTR"\n",mode,rotation,audit.samples[i].us,audit.samples[i].kind,audit.samples[i].given,audit.samples[i].source,audit.samples[i].target,audit.samples[i].position,audit.samples[i].request_seq,audit.samples[i].event_seq,audit.samples[i].raw);
}

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
