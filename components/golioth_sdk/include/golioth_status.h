#pragma once

#define FOREACH_GOLIOTH_STATUS(STATUS) \
    STATUS(GOLIOTH_OK) \
    STATUS(GOLIOTH_ERR_DNS_LOOKUP) \
    STATUS(GOLIOTH_ERR_NOT_IMPLEMENTED) \
    STATUS(GOLIOTH_ERR_MEM_ALLOC) \
    STATUS(GOLIOTH_ERR_NULL) \
    STATUS(GOLIOTH_ERR_INVALID_FORMAT) \
    STATUS(GOLIOTH_ERR_SERIALIZE) \
    STATUS(GOLIOTH_ERR_IO) \
    STATUS(GOLIOTH_ERR_TIMEOUT) \
    STATUS(GOLIOTH_ERR_QUEUE_FULL) \
    STATUS(GOLIOTH_ERR_NOT_ALLOWED)

#define GENERATE_GOLIOTH_STATUS_ENUM(code) code,
typedef enum {
    FOREACH_GOLIOTH_STATUS(GENERATE_GOLIOTH_STATUS_ENUM)
    NUM_GOLIOTH_STATUS_CODES
} golioth_status_t;

const char* golioth_status_to_str(golioth_status_t status);

#define GOLIOTH_STATUS_RETURN_IF_ERROR(expr) \
    do { \
        golioth_status_t status = (expr); \
        if (status != GOLIOTH_OK) { \
            return status; \
        } \
    } while(0)
