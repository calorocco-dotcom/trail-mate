#pragma once
using gpio_num_t = int;
constexpr int GPIO_MODE_INPUT = 1;
constexpr int GPIO_PULLDOWN_ONLY = 2;
inline int gpio_set_direction(gpio_num_t, int) { return 0; }
inline int gpio_set_pull_mode(gpio_num_t, int) { return 0; }
inline int gpio_get_level(gpio_num_t) { return 1; }
