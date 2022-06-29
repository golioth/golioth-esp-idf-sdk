/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

// For Golioth APIs that take a timeout parameter
#define GOLIOTH_WAIT_FOREVER -1

// Time since boot
uint64_t golioth_time_micros(void);
uint64_t golioth_time_millis(void);

void golioth_time_delay_ms(uint32_t ms);
