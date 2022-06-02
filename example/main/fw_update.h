#pragma once

#include "golioth.h"

// Create a task that will perform firmware updates
void fw_update_init(golioth_client_t client, const char* current_version);
