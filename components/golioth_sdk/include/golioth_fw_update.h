/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "golioth.h"

// Create a task that will perform firmware updates
void golioth_fw_update_init(golioth_client_t client, const char* current_version);
