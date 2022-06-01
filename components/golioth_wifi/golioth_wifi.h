#pragma once

void golioth_wifi_init_with_credentials(const char* ssid, const char* password);
void golioth_wifi_init(void);
void golioth_wifi_clear_credentials(void);
void golioth_wifi_set_credentials_and_connect(const char* ssid, const char* password);
void golioth_wifi_get_credentials(const char** ssid, const char** password);
void golioth_wifi_wait_for_connected(void);
