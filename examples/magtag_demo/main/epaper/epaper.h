#pragma once

#include <stdint.h>

// TODO - more efficient driver, use SPI peripheral instead of bit-banging IO

void epaper_init(void);
void epaper_autowrite(uint8_t* str);
