/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

void wifi_init(const char* ssid, const char* password);
void wifi_wait_for_connected(void);
