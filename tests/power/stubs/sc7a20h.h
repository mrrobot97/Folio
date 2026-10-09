#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void* sc7a20h_handle_t;
typedef struct {uint8_t aoi1_src;} sc7a20h_events_t;
typedef struct {bool ia;} sc7a20h_aoi_src_t;
typedef struct {int ths_mg,duration;} sc7a20h_motion_cfg_t;
#define SC7A20H_MOTION_DEFAULT() ((sc7a20h_motion_cfg_t){0})
esp_err_t sc7a20h_read_events(sc7a20h_handle_t,sc7a20h_events_t*);
void sc7a20h_ack_int(sc7a20h_handle_t);
void sc7a20h_aoi_decode(uint8_t,sc7a20h_aoi_src_t*);
int sc7a20h_int1_level(sc7a20h_handle_t);
int sc7a20h_int1_gpio(sc7a20h_handle_t);
void sc7a20h_arm_pickup_wake(sc7a20h_handle_t,const sc7a20h_motion_cfg_t*);
void sc7a20h_config_light_sleep_wakeup(sc7a20h_handle_t);
void sc7a20h_power_down(sc7a20h_handle_t);
void sc7a20h_int1_begin(sc7a20h_handle_t);
