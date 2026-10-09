#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct {unsigned key_state;} pmu_snapshot_t;
const pmu_snapshot_t*read_pico_pmu_get(void);
bool read_pico_pmu_take_key_short(void);
bool read_pico_pmu_take_key_wakeup(void);
void read_pico_pmu_drain_events(void);
esp_err_t read_pico_pmu_poll(void);
esp_err_t read_pico_pmu_power_off(void);
esp_err_t read_pico_pmu_report_sleep(void);
