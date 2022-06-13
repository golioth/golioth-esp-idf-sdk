#pragma once

#include <stdint.h>

void buttons_gpio_init(void);
void buttons_handle_event(uint32_t button_events);
