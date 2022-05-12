#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h> // struct addrinfo
#include <sys/param.h> // MIN
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <coap3/coap.h>
#include "golioth_client.h"
#include "golioth_coap_client.h"

#define TAG "golioth_coap_client"

//
// Golioth SDK config
//
#define CONFIG_GOLIOTH_COAP_HOST_URI "coaps://coap.golioth.io"

// Maximum time, in milliseconds, the coap_task will wait for something
// to arrive in the request queue.
#define CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_TIMEOUT_MS 1000

// Maximum time, in milliseconds, the coap_task will block while waiting
// for a response from the server.
#define CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_MS 10000

// Maximum number of items the coap_task request queue will hold.
// If the queue is full, any attempts to queue new messages will fail.
#define CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_MAX_ITEMS 10

static bool _initialized;

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

    coap_context_t* coap_context = coap_session_get_context(session);
    golioth_coap_client_t* client = (golioth_coap_client_t*)coap_get_app_data(coap_context);
    client->got_coap_response = true;

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

static void golioth_coap_get(const golioth_coap_get_params_t* params, coap_session_t* session) {
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

static void golioth_coap_put(const golioth_coap_put_params_t* params, coap_session_t* session) {
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

static void golioth_coap_delete(const golioth_coap_delete_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_DELETE, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() delete failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path);
    coap_send(session, request);
}

static void golioth_coap_observe(const golioth_coap_observe_params_t* params, coap_session_t* session) {
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
static void golioth_coap_client_task(void *arg) {
    golioth_coap_client_t* client = (golioth_coap_client_t*)arg;

    coap_context_t* coap_context = NULL;
    coap_session_t* coap_session = NULL;

    coap_context = coap_new_context(NULL);
    if (!coap_context) {
        ESP_LOGE(TAG, "Failed to create CoAP context");
        goto clean_up;
    }

    // Store out client pointer in the context, since it's needed in the reponse handler
    // we register below.
    coap_set_app_data(coap_context, client);

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
        .psk_info.identity.s = (const uint8_t*)client->psk_id,
        .psk_info.identity.length = client->psk_id_len,
        .psk_info.key.s = (const uint8_t*)client->psk,
        .psk_info.key.length = client->psk_len,
    };
    coap_session = coap_new_client_session_psk2(
            coap_context, NULL, &dst_addr, COAP_PROTO_DTLS, &dtls_psk);
    if (!coap_session) {
        ESP_LOGE(TAG, "coap_new_client_session() failed");
        goto clean_up;
    }

    ESP_LOGD(TAG, "Entering CoAP I/O loop");
    golioth_coap_request_msg_t request_msg = {};
    while (!client->coap_task_shutdown) {
        // Wait for request message, with timeout
        bool got_request_msg = xQueueReceive(
                client->request_queue,
                &request_msg,
                CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (got_request_msg) {
            client->got_coap_response = false;

            // Handle message and send request to server
            switch (request_msg.type) {
                case GOLIOTH_COAP_REQUEST_GET:
                    ESP_LOGD(TAG, "Handle GET %s", request_msg.get.path);
                    golioth_coap_get(&request_msg.get, coap_session);
                    break;
                case GOLIOTH_COAP_REQUEST_PUT:
                    ESP_LOGD(TAG, "Handle PUT %s", request_msg.put.path);
                    golioth_coap_put(&request_msg.put, coap_session);
                    assert(request_msg.put.payload);
                    free(request_msg.put.payload);
                    break;
                case GOLIOTH_COAP_REQUEST_DELETE:
                    ESP_LOGD(TAG, "Handle DELETE %s", request_msg.delete.path);
                    golioth_coap_delete(&request_msg.delete, coap_session);
                    break;
                case GOLIOTH_COAP_REQUEST_OBSERVE:
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
                } else if (client->got_coap_response) {
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
                // TODO - How to handle this? Should we wait for WiFi and re-establish session?
            }
        }
    }

clean_up:
    // Clear flag, in case the task gets restarted later
    client->coap_task_shutdown = false;

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

golioth_client_t golioth_client_create(const char* psk_id, const char* psk) {
    if (!_initialized) {
        // Connect logs from libcoap to the ESP logger
        coap_set_log_handler(coap_log_handler);
        coap_set_log_level(6); // 3: error, 4: warning, 6: info, 7: debug
        _initialized = true;
    }

    golioth_coap_client_t* new_client = calloc(1, sizeof(golioth_coap_client_t));
    if (!new_client) {
        ESP_LOGE(TAG, "Failed to allocate memory for client");
        goto error;
    }

    new_client->psk_id = psk_id;
    new_client->psk_id_len = strlen(psk_id);
    new_client->psk = psk;
    new_client->psk_len = strlen(psk);

    new_client->request_queue = xQueueCreate(
            CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_MAX_ITEMS,
            sizeof(golioth_coap_request_msg_t));
    if (!new_client->request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        goto error;
    }

    // TODO - make the priority and stack size configurable
    bool task_created = xTaskCreate(
            golioth_coap_client_task,
            "coap_client",
            8 * 1024, // stack size in bytes
            new_client,  // task arg
            5,  // priority
            &new_client->coap_task_handle);
    if (!task_created) {
        ESP_LOGE(TAG, "Failed to create client task");
        goto error;
    }

    return (golioth_client_t)new_client;

error:
    if (new_client) {
        golioth_client_destroy(new_client);
    }
    return NULL;
}

golioth_status_t golioth_client_start(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }
    return GOLIOTH_ERR_NOT_IMPLEMENTED;
}

golioth_status_t golioth_client_stop(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }
    return GOLIOTH_ERR_NOT_IMPLEMENTED;
}

golioth_status_t golioth_client_restart(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }
    return GOLIOTH_ERR_NOT_IMPLEMENTED;
}

void golioth_client_destroy(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (c->request_queue) {
        vQueueDelete(c->request_queue);
    }
    if (c->coap_task_handle) {
        vTaskDelete(c->coap_task_handle);
    }
    free(c);
}
