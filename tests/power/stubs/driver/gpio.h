#pragma once
#include <stdint.h>
typedef int gpio_num_t;
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_LOW_LEVEL 0
void gpio_config(const gpio_config_t*);
void gpio_wakeup_enable(gpio_num_t,int);
void gpio_wakeup_disable(gpio_num_t);
int gpio_get_level(gpio_num_t);
