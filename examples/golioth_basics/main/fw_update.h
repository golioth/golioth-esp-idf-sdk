#pragma once

#include "golioth.h"
#include <stdbool.h>

void fw_update_init(golioth_client_t client, const char* current_version);
bool fw_update_is_pending_verify(void);
void fw_update_rollback_and_reboot(void);
void fw_update_cancel_rollback(void);
bool fw_update_manifest_version_is_different(const golioth_ota_manifest_t* manifest);
golioth_status_t fw_update_download_and_write_flash(void);
golioth_status_t fw_update_validate(void);
golioth_status_t fw_update_change_boot_image(void);
void fw_update_end(void);
