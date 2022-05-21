#include <string.h>
#include "golioth_ota.h"

golioth_status_t golioth_ota_payload_as_manifest(
        const uint8_t* payload,
        size_t payload_len,
        golioth_ota_manifest_t* manifest) {
    // TODO
    return GOLIOTH_OK;
}

size_t golioth_ota_size_to_nblocks(size_t component_size) {
    size_t nblocks = component_size / GOLIOTH_OTA_BLOCKSIZE;
    if ((component_size % GOLIOTH_OTA_BLOCKSIZE) != 0) {
        nblocks++;
    }
    return nblocks;
}

const golioth_ota_component_t* golioth_ota_find_component(const golioth_ota_manifest_t* manifest, const char* package) {
    // Scan the manifest until we find the component with matching package.
    const golioth_ota_component_t* found = NULL;
    for (int i = 0; i < manifest->num_components; i++) {
        const golioth_ota_component_t* c = &manifest->components[i];
        bool matches = (0 == strcmp(c->package, package));
        if (matches) {
            found = c;
            break;
        }
    }
    return found;
}

golioth_status_t golioth_ota_observe_manifest(
        golioth_client_t client,
        golioth_get_cb_fn callback,
        void* arg) {
    // TODO
    return GOLIOTH_OK;
}

golioth_status_t golioth_ota_get_manifest(
        golioth_client_t client,
        golioth_ota_manifest_t* manifest) {
    // TODO
    return GOLIOTH_OK;
}

golioth_status_t golioth_ota_get_block(
        golioth_client_t client,
        const char* package,
        const char* version,
        size_t block_index,
        uint8_t* buf,  // must be at least GOLIOTH_OTA_BLOCKSIZE bytes
        size_t* block_nbytes,
        size_t* offset) {
    // TODO
    return GOLIOTH_OK;
}

golioth_status_t golioth_ota_report_state(
        golioth_client_t client,
        golioth_ota_state_t state,
        golioth_ota_reason_t reason,
        const char* package,
        const char* current_version,
        const char* target_version) {
    // TODO
    return GOLIOTH_OK;
}
