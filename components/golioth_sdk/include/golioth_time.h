/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

// Time since boot
uint64_t golioth_time_micros();
uint64_t golioth_time_millis();

void golioth_time_delay_ms(uint32_t ms);
