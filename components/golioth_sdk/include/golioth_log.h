#pragma once

#include "golioth_status.h"
#include "golioth_client.h"

golioth_status_t golioth_log(golioth_client_t client, const char* log_message);
