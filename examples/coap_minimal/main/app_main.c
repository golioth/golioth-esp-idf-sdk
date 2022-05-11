#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "coap3/coap.h"
#include "golioth_status.h"

#define TAG "coap_minimal"

//
// Example config
//
#define EXAMPLE_COAP_PSK_ID "nicks_esp32s3_devkit-id@nicks-first-project"
#define EXAMPLE_COAP_PSK "8e2b614316c6db46146ebfff44cd649f"

//
// Golioth SDK config
//

// Maximum time, in milliseconds, the coap_task will wait for something
// to arrive in the request queue.
#define CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_TIMEOUT_MS 1000

// Maximum time, in milliseconds, the coap_task will block while waiting
// for a response from the server.
#define CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_MS 10000

// Maximum number of items the coap_task request queue will hold.
// If the queue is full, any attempts to queue new messages will fail.
#define CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_MAX_ITEMS 10

#define CONFIG_GOLIOTH_COAP_HOST_URI "coaps://coap.golioth.io"

typedef struct {
    // The CoAP path string (everything after coaps://coap.golioth.io/).
    // Assumption: path is a string literal (i.e. we don't need to strcpy).
    const char* path;
    // Must be one of:
    //   COAP_MEDIATYPE_APPLICATION_JSON
    //   COAP_MEDIATYPE_APPLICATION_CBOR
    uint32_t content_type;
    // CoAP payload assumed to be dynamically allocated before enqueue
    // and freed after dequeue.
    uint8_t* payload;
    // Size of payload, in bytes
    size_t payload_size;
} put_params_t;

typedef struct {
    const char* path;
    uint32_t content_type;
} get_params_t;

typedef struct {
    const char* path;
} delete_params_t;

typedef struct {
    const char* path;
    uint32_t content_type;
} observe_params_t;

// Internal struct, not user-facing
typedef struct {
    // Must be one of:
    //  COAP_REQUEST_GET
    //  COAP_REQUEST_PUT
    //  COAP_REQUEST_DELETE
    //  COAP_OPTION_OBSERVE
    uint32_t type;
    union {
        get_params_t get;
        put_params_t put;
        delete_params_t delete;
        observe_params_t observe;
    };
} coap_request_msg_t;

typedef struct {
    TaskHandle_t coap_task_handle;
    QueueHandle_t request_queue;
    bool coap_task_shutdown;
    bool got_coap_response;
} golioth_coap_client_t;

static golioth_coap_client_t _client;

static coap_response_t coap_response_handler(
        coap_session_t* session,
        const coap_pdu_t* sent,
        const coap_pdu_t* received,
        const coap_mid_t mid) {
    coap_pdu_code_t rcvd_code = coap_pdu_get_code(received);
    ESP_LOGI(TAG, "%d.%02d", (rcvd_code >> 5), rcvd_code & 0x1F);

    const unsigned char *data = NULL;
    size_t data_len = 0;
    size_t offset = 0;
    size_t total = 0;
    if (coap_get_data_large(received, &data_len, &data, &offset, &total)) {
        printf(": ");
        while(data_len--) {
            printf("%c", isprint(*data) ? *data : '.');
            data++;
        }
    }
    printf("\n");

    ESP_LOGD(TAG, "got response");
    _client.got_coap_response = true;
    return COAP_RESPONSE_OK;
}

static void coap_log_handler(coap_log_t level, const char *message) {
    ESP_LOGI("libcoap", "%s", message);
}

// DNS lookup of host_uri
static golioth_status_t get_coap_dst_address(const coap_uri_t* host_uri, coap_address_t* dst_addr) {
    struct addrinfo hints = {
        .ai_socktype = SOCK_DGRAM,
        .ai_family = AF_UNSPEC,
    };
    struct addrinfo* ainfo = NULL;
    const char* hostname = (const char*)host_uri->host.s;
    int error = getaddrinfo(hostname, NULL, &hints, &ainfo);
    if (error != 0) {
        ESP_LOGE(TAG, "DNS lookup failed for destination ainfo %s. error: %d", hostname, error);
        return GOLIOTH_ERR_DNS_LOOKUP;
    }
    if (!ainfo) {
        ESP_LOGE(TAG, "DNS lookup %s did not return any addresses", hostname);
        return GOLIOTH_ERR_DNS_LOOKUP;
    }

    coap_address_init(dst_addr);

    switch (ainfo->ai_family) {
    case AF_INET:
        memcpy(&dst_addr->addr.sin, ainfo->ai_addr, sizeof(dst_addr->addr.sin));
        dst_addr->addr.sin.sin_port = htons(host_uri->port);
        break;
    case AF_INET6:
        memcpy(&dst_addr->addr.sin6, ainfo->ai_addr, sizeof(dst_addr->addr.sin6));
        dst_addr->addr.sin6.sin6_port = htons(host_uri->port);
        break;
    default:
        ESP_LOGE(TAG, "DNS lookup response failed");
        freeaddrinfo(ainfo);
        return GOLIOTH_ERR_DNS_LOOKUP;
    }
    freeaddrinfo(ainfo);

    return GOLIOTH_OK;
}

static void golioth_coap_add_token(coap_pdu_t* request, coap_session_t* session) {
    size_t tokenlength = 0;
    unsigned char token[8];
    coap_session_new_token(session, &tokenlength, token);
    coap_add_token(request, tokenlength, token);
}

static void golioth_coap_add_path(coap_pdu_t* request, const char* path) {
    size_t pathlen = strlen(path);
    unsigned char buf[64];
    unsigned char *pbuf = buf;
    size_t buflen = sizeof(buf);
    int nsegments = coap_split_path((const uint8_t*)path, pathlen, pbuf, &buflen);
    while (nsegments--) {
        coap_add_option(request, COAP_OPTION_URI_PATH, coap_opt_length(pbuf), coap_opt_value(pbuf));
        pbuf += coap_opt_size(pbuf);
    }
}

static void golioth_coap_add_content_type(coap_pdu_t* request, uint32_t content_type) {
    unsigned char typebuf[4];
    coap_add_option(
            request,
            COAP_OPTION_CONTENT_TYPE,
            coap_encode_var_safe(typebuf, sizeof(typebuf), content_type),
            typebuf);
}

static void golioth_coap_get(const get_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path);
    golioth_coap_add_content_type(request, params->content_type);
    coap_send(session, request);
}

static void golioth_coap_put(const put_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_PUT, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() put failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path);
    golioth_coap_add_content_type(request, params->content_type);
    coap_add_data(request, params->payload_size, (unsigned char*)params->payload);
    coap_send(session, request);
}

static void golioth_coap_delete(const delete_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_DELETE, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() delete failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path);
    coap_send(session, request);
}

static void golioth_coap_observe(const observe_params_t* params, coap_session_t* session) {
    // GET with an OBSERVE option
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path);
    coap_add_option(request, COAP_OPTION_OBSERVE, 0, NULL);
    golioth_coap_add_content_type(request, params->content_type);
    coap_send(session, request);
}

// Note: libcoap is not thread safe, so all rx/tx I/O for the session must be
// done in this task.
static void coap_client_task(void *arg) {
    coap_context_t* coap_context = NULL;
    coap_session_t* coap_session = NULL;

    coap_context = coap_new_context(NULL);
    assert(coap_context);

    // Enable block mode, required for Golioth DFU
    coap_context_set_block_mode(
            coap_context,
            COAP_BLOCK_USE_LIBCOAP|COAP_BLOCK_SINGLE_BODY);

    // Register handler for all responses
    coap_register_response_handler(coap_context, coap_response_handler);

    // Split URI for host
    coap_uri_t host_uri = {};
    int uri_status = coap_split_uri(
            (const uint8_t*)CONFIG_GOLIOTH_COAP_HOST_URI,
            strlen(CONFIG_GOLIOTH_COAP_HOST_URI),
            &host_uri);
    if (uri_status < 0) {
        ESP_LOGE(TAG, "CoAP host URI invalid: %s", CONFIG_GOLIOTH_COAP_HOST_URI);
        goto clean_up;
    }

    // Get destination address of host
    coap_address_t dst_addr = {};
    if (get_coap_dst_address(&host_uri, &dst_addr) != GOLIOTH_OK) {
        goto clean_up;
    }

    ESP_LOGI(TAG, "Start CoAP session");
    char client_sni[256] = {};
    memcpy(client_sni, host_uri.host.s, MIN(host_uri.host.length, sizeof(client_sni) - 1));
    coap_dtls_cpsk_t dtls_psk = {
        .version = COAP_DTLS_CPSK_SETUP_VERSION,
        .client_sni = client_sni,
        .psk_info.identity.s = (const uint8_t*)EXAMPLE_COAP_PSK_ID,
        .psk_info.identity.length = sizeof(EXAMPLE_COAP_PSK_ID) - 1,
        .psk_info.key.s = (const uint8_t*)EXAMPLE_COAP_PSK,
        .psk_info.key.length = sizeof(EXAMPLE_COAP_PSK) - 1,
    };
    coap_session = coap_new_client_session_psk2(
            coap_context, NULL, &dst_addr, COAP_PROTO_DTLS, &dtls_psk);
    if (!coap_session) {
        ESP_LOGE(TAG, "coap_new_client_session() failed");
        goto clean_up;
    }

    ESP_LOGD(TAG, "Entering CoAP I/O loop");
    coap_request_msg_t request_msg = {};
    while (!_client.coap_task_shutdown) {
        // Wait for request message, with timeout
        bool got_request_msg = xQueueReceive(
                _client.request_queue,
                &request_msg,
                CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (got_request_msg) {
            _client.got_coap_response = false;

            // Handle message and send request to server
            switch (request_msg.type) {
                case COAP_REQUEST_GET:
                    ESP_LOGD(TAG, "Handle GET %s", request_msg.get.path);
                    golioth_coap_get(&request_msg.get, coap_session);
                    break;
                case COAP_REQUEST_PUT:
                    ESP_LOGD(TAG, "Handle PUT %s", request_msg.put.path);
                    golioth_coap_put(&request_msg.put, coap_session);
                    assert(request_msg.put.payload);
                    free(request_msg.put.payload);
                    break;
                case COAP_REQUEST_DELETE:
                    ESP_LOGD(TAG, "Handle DELETE %s", request_msg.delete.path);
                    golioth_coap_delete(&request_msg.delete, coap_session);
                    break;
                case COAP_OPTION_OBSERVE:
                    ESP_LOGD(TAG, "Handle OBSERVE %s", request_msg.observe.path);
                    golioth_coap_observe(&request_msg.observe, coap_session);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown request_msg type: %u", request_msg.type);
                    break;
            }

            // Wait for response from server
            uint32_t time_spent_waiting_ms = 0;
            while (time_spent_waiting_ms < CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_MS) {
                int num_ms = coap_io_process(coap_context, CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_MS);
                if (num_ms < 0) {
                    ESP_LOGE(TAG, "Error in coap_io_process: %d", num_ms);
                    // TODO - How to handle this? Should we wait for WiFi and re-establish session?
                    break;
                } else if (_client.got_coap_response) {
                    ESP_LOGD(TAG, "Received response in %d ms", num_ms);
                    break;
                } else {
                    // During normal operation, there will be other kinds of IO to process,
                    // in which case we will get here.
                    //
                    // Since we haven't received the response yet, just keep waiting.
                    time_spent_waiting_ms += num_ms;
                }
            }

            if (time_spent_waiting_ms >= CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_MS) {
                ESP_LOGE(TAG, "Timeout: never got a response from the server");
            }
        }
    }

clean_up:
    // Clear flag, in case the task gets restarted later
    _client.coap_task_shutdown = false;

    if (coap_session) {
        coap_session_release(coap_session);
    }
    if (coap_context) {
        coap_free_context(coap_context);
    }
    coap_cleanup();
    ESP_LOGI(TAG, "Task exiting");
    vTaskDelete(NULL);
}

void app_main(void) {
    // Initialization required for connecting to WiFi
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Connect logs from libcoap to the ESP logger
    coap_set_log_handler(coap_log_handler);
    coap_set_log_level(6); // 3: error, 4: warning, 6: info, 7: debug

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * $ENV{IDF_PATH}/examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    _client.request_queue = xQueueCreate(CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_MAX_ITEMS, sizeof(coap_request_msg_t));
    assert(_client.request_queue);

    bool task_created = xTaskCreate(
            coap_client_task,
            "coap_client",
            8 * 1024, // stack size in bytes (technically words, but on esp-idf words are bytes)
            NULL,  // task arg
            5,  // priority
            &_client.coap_task_handle);
    assert(task_created);

    ESP_LOGI(TAG, "OBSERVE \".d/setting\"");
    {
        coap_request_msg_t request_msg = {
            .type = COAP_OPTION_OBSERVE,
            .observe = {
                .path = ".d/setting",
                .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
            },
        };
        BaseType_t sent = xQueueSend(_client.request_queue, &request_msg, portMAX_DELAY);
        assert(sent == pdTRUE);
    }

    // Send a log every 10 seconds
    int iteration = 0;
    while (1) {
        ESP_LOGI(TAG, "PUT \"logs\"");
        {
            const char* json_log = "{\"level\":\"info\",\"module\":\"example\",\"msg\":\"test\"}";
            size_t json_log_len = strlen(json_log);
            uint8_t* payload = (uint8_t*)calloc(1, json_log_len);
            assert(payload);
            memcpy(payload, json_log, json_log_len);

            coap_request_msg_t request_msg = {
                .type = COAP_REQUEST_PUT,
                .put = {
                    .path = "logs",
                    .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
                    .payload = payload,
                    .payload_size = json_log_len,
                },
            };
            BaseType_t sent = xQueueSend(_client.request_queue, &request_msg, portMAX_DELAY);
            assert(sent == pdTRUE);
        }

        ESP_LOGI(TAG, "PUT \".d/iteration\"");
        {
            char buf[16];
            sprintf(buf, "%d", iteration);
            size_t buflen = strlen(buf);
            uint8_t* payload = (uint8_t*)calloc(1, buflen);
            assert(payload);
            memcpy(payload, buf, buflen);
            coap_request_msg_t request_msg = {
                .type = COAP_REQUEST_PUT,
                .put = {
                    .path = ".d/iteration",
                    .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
                    .payload = payload,
                    .payload_size = buflen,
                },
            };
            BaseType_t sent = xQueueSend(_client.request_queue, &request_msg, portMAX_DELAY);
            assert(sent == pdTRUE);
        }

        ESP_LOGI(TAG, "GET \".d\"");
        {
            coap_request_msg_t request_msg = {
                .type = COAP_REQUEST_GET,
                .get = {
                    .path = ".d",
                    .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
                },
            };
            BaseType_t sent = xQueueSend(_client.request_queue, &request_msg, portMAX_DELAY);
            assert(sent == pdTRUE);
        }

        ESP_LOGI(TAG, "GET \".d/nonexistant\"");
        {
            coap_request_msg_t request_msg = {
                .type = COAP_REQUEST_GET,
                .get = {
                    .path = ".d/nonexistant",
                    .content_type = COAP_MEDIATYPE_APPLICATION_JSON,
                },
            };
            BaseType_t sent = xQueueSend(_client.request_queue, &request_msg, portMAX_DELAY);
            assert(sent == pdTRUE);
        }

        ESP_LOGI(TAG, "DELETE \".d/delete_me\"");
        {
            coap_request_msg_t request_msg = {
                .type = COAP_REQUEST_DELETE,
                .delete = {
                    .path = ".d/delete_me",
                },
            };
            BaseType_t sent = xQueueSend(_client.request_queue, &request_msg, portMAX_DELAY);
            assert(sent == pdTRUE);
        }

        uint32_t free_heap = xPortGetFreeHeapSize();
        uint32_t min_free_heap = xPortGetMinimumEverFreeHeapSize();
        ESP_LOGI(TAG, "Free heap = %u bytes, Min ever free heap = %u", free_heap, min_free_heap);
        ESP_LOGI(TAG, "app_main delaying for 10s...");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        iteration++;
    };
}
