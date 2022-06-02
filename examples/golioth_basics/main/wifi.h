#pragma once

void wifi_init_with_credentials(const char* ssid, const char* password);
void wifi_init(void);
void wifi_clear_credentials(void);
void wifi_set_credentials_and_connect(const char* ssid, const char* password);
void wifi_get_credentials(const char** ssid, const char** password);
void wifi_wait_for_connected(void);
