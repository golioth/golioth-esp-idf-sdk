/// Statistics internal to the Golioth SDK, for debug and troubleshoot of the SDK itself.
#pragma once

#include <stdint.h>

typedef struct {
    int32_t total_allocd_bytes;
    int32_t total_freed_bytes;
} golioth_stats_t;

extern golioth_stats_t g_golioth_stats;
