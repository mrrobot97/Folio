#pragma once
#include <stdbool.h>
typedef struct {bool status_ok;unsigned soc_permille;} pmu_snapshot_t;
const pmu_snapshot_t* read_pico_pmu_get(void);
