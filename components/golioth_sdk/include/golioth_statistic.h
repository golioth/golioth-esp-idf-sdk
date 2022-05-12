/// Statistics internal to the Golioth SDK, for debug and troubleshoot of the SDK itself.
#pragma once

#include <stdint.h>

#define FOREACH_GOLIOTH_STATISTIC(STATISTIC) \
    STATISTIC(GSTAT_ID_ALLOCATED_BYTES,     "allocd") \
    STATISTIC(GSTAT_ID_FREED_BYTES,         "freed")

#define GENERATE_GOLIOTH_STATISTIC_ENUM(id, _) id,
typedef enum {
    FOREACH_GOLIOTH_STATISTIC(GENERATE_GOLIOTH_STATISTIC_ENUM)
    NUM_GOLIOTH_STATISTIC_IDS
} golioth_statistic_id_t;

// Add (or subtract) a value from the given statistic id
void golioth_statistic_add(golioth_statistic_id_t id, int32_t value);

// Set a stat ID to a specific value
void golioth_statistic_set(golioth_statistic_id_t id, int32_t value);

// Print all statistics to console
void golioth_statistic_print_all(void);
