#pragma once
#include <stdint.h>
#include "esp_timer.h"
extern uint64_t pp_us[16];
extern unsigned pp_count[16];
static inline void pp_add(unsigned index, uint64_t start) {
 pp_us[index] += (uint64_t)esp_timer_get_time() - start;
 ++pp_count[index];
}
