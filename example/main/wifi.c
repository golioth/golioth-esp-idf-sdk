#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "wifi.h"

#define TAG "example_wifi"

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRIES 5

// NVS keys for wifi credentials
#define WIFI_NVS_NAMESPACE "wifi"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASS_KEY "pass"

#define WIFI_DEFAULT_SSID "unknown"
#define WIFI_DEFAULT_PASS "unknown"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

static const char* read_nvs_key_or_default(const char* key, char* out, size_t outsize, const char* defaultstr) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed");
        return defaultstr;
    }
    size_t bytes_read = outsize;
    err = nvs_get_str(handle, key, out, &bytes_read);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_str key %s failed, err = %d", key, err);
    }
    nvs_close(handle);
    return (err == ESP_OK ? out : defaultstr);
}

static void write_nvs_key(const char* key, const char* str) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed");
        return;
    }
    err = nvs_set_str(handle, key, str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set %s to %s, err = %d", key, str, err);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

static void erase_nvs_key(const char* key) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed");
        return;
    }
    err = nvs_erase_key(handle, key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase key %s, err = %d", key, err);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

static const char* read_nvs_ssid(void) {
    static char ssidbuf[32];
    return read_nvs_key_or_default(WIFI_SSID_KEY, ssidbuf, sizeof(ssidbuf), WIFI_DEFAULT_SSID);
}

static const char* read_nvs_pass(void) {
    static char passbuf[32];
    return read_nvs_key_or_default(WIFI_PASS_KEY, passbuf, sizeof(passbuf), WIFI_DEFAULT_PASS);
}

static void write_nvs_ssid(const char* ssid) {
    write_nvs_key(WIFI_SSID_KEY, ssid);
}

static void write_nvs_pass(const char* password) {
    write_nvs_key(WIFI_PASS_KEY, password);
}

static void connect(void) {
    if (0 == strcmp(read_nvs_ssid(), WIFI_DEFAULT_SSID)) {
        ESP_LOGW(TAG, "Can't connect, WiFi SSID not set");
        return;
    }
    if (0 == strcmp(read_nvs_pass(), WIFI_DEFAULT_PASS)) {
        ESP_LOGW(TAG, "Can't connect, WiFi password not set");
        return;
    }
    esp_wifi_connect();
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_num < WIFI_MAX_RETRIES) {
            connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi Connected. Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_internal(const char* ssid, const char* password) {
    if (s_wifi_event_group) {
        ESP_LOGW(TAG, "wifi_init is not intended to be called more than once");
        return;
    }
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &event_handler,
                NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
                IP_EVENT,
                IP_EVENT_STA_GOT_IP,
                &event_handler,
                NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    ESP_ERROR_CHECK(esp_wifi_start() );
}

void wifi_init_with_credentials(const char* ssid, const char* password) {
    ESP_ERROR_CHECK(nvs_flash_init());
    write_nvs_ssid(ssid);
    write_nvs_pass(password);
    wifi_init_internal(read_nvs_ssid(), read_nvs_pass());
}

void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_internal(read_nvs_ssid(), read_nvs_pass());
}

void wifi_set_credentials_and_connect(const char* ssid, const char* password) {
    write_nvs_ssid(ssid);
    write_nvs_pass(password);

    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    s_retry_num = 0;
    connect();
}

void wifi_get_credentials(const char** ssid, const char** password) {
    *ssid = read_nvs_ssid();
    *password = read_nvs_pass();
}

void wifi_clear_credentials(void) {
    erase_nvs_key(WIFI_SSID_KEY);
    erase_nvs_key(WIFI_PASS_KEY);
}

void wifi_wait_for_connected(void) {
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    const char* ssid = read_nvs_ssid();
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

