/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>      // struct addrinfo
#include <sys/param.h>  // MIN
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <coap3/coap.h>
#include "golioth_client.h"
#include "golioth_coap_client.h"
#include "golioth_stats.h"
#include "golioth_util.h"
#include "golioth_lightdb.h"

#define TAG "golioth_coap_client"

static bool _initialized;
golioth_stats_t g_golioth_stats;

#define GOLIOTH_DEFAULT_PSK_ID "unknown"
#define GOLIOTH_DEFAULT_PSK "unknown"

// This is the struct hidden by the opaque type golioth_client_t
// TODO - document these
typedef struct {
    QueueHandle_t request_queue;
    TaskHandle_t coap_task_handle;
    SemaphoreHandle_t run_sem;
    TimerHandle_t keepalive_timer;
    int keepalive_count;
    bool end_session;
    bool session_connected;
    uint8_t token[8];  // token of the pending request
    size_t token_len;
    bool got_coap_response;
    const char* psk_id;
    size_t psk_id_len;
    const char* psk;
    size_t psk_len;
    golioth_coap_request_msg_t pending_req;
    golioth_coap_observe_info_t observations[CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS];
    // token to use for block GETs (must use same token for all blocks)
    uint8_t block_token[8];
    size_t block_token_len;
    golioth_client_event_cb_fn event_callback;
    void* event_callback_arg;
} golioth_coap_client_t;

static bool token_matches_request(const coap_pdu_t* received, golioth_coap_client_t* client) {
    coap_bin_const_t rcvd_token = coap_pdu_get_token(received);
    bool len_matches = (rcvd_token.length == client->token_len);
    return (len_matches && (0 == memcmp(rcvd_token.s, client->token, client->token_len)));
}

static void notify_observers(
        const coap_pdu_t* received,
        golioth_coap_client_t* client,
        const uint8_t* data,
        size_t data_len) {
    // scan observations, check for token match
    for (int i = 0; i < CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS; i++) {
        const golioth_coap_observe_info_t* obs_info = &client->observations[i];
        golioth_get_cb_fn callback = obs_info->req_params.callback;

        if (!obs_info->in_use || !callback) {
            continue;
        }

        coap_bin_const_t rcvd_token = coap_pdu_get_token(received);
        bool len_matches = (rcvd_token.length == obs_info->token_len);
        if (len_matches && (0 == memcmp(rcvd_token.s, obs_info->token, obs_info->token_len))) {
            callback(client, obs_info->req_params.path, data, data_len, obs_info->req_params.arg);
        }
    }
}

static coap_response_t coap_response_handler(
        coap_session_t* session,
        const coap_pdu_t* sent,
        const coap_pdu_t* received,
        const coap_mid_t mid) {
    coap_pdu_code_t rcvd_code = coap_pdu_get_code(received);
    coap_pdu_type_t rcv_type = coap_pdu_get_type(received);
    ESP_LOGD(TAG, "%d.%02d", (rcvd_code >> 5), rcvd_code & 0x1F);

    if (rcv_type == COAP_MESSAGE_RST) {
        ESP_LOGW(TAG, "Got RST");
        return COAP_RESPONSE_OK;
    }

    coap_context_t* coap_context = coap_session_get_context(session);
    golioth_coap_client_t* client = (golioth_coap_client_t*)coap_get_app_data(coap_context);


    const uint8_t* data = NULL;
    size_t data_len = 0;
    coap_get_data(received, &data_len, &data);

    if (token_matches_request(received, client)) {
        client->got_coap_response = true;

        if (CONFIG_GOLIOTH_COAP_KEEPALIVE_INTERVAL_S > 0) {
            if (!xTimerReset(client->keepalive_timer, 0)) {
                ESP_LOGW(TAG, "Failed to reset keepalive timer");
            }
        }

        if (client->pending_req.type == GOLIOTH_COAP_REQUEST_GET) {
            if (client->pending_req.get.callback) {
                client->pending_req.get.callback(
                        client,
                        client->pending_req.get.path,
                        data,
                        data_len,
                        client->pending_req.get.arg);
            }
        } else if (client->pending_req.type == GOLIOTH_COAP_REQUEST_GET_BLOCK) {
            if (client->pending_req.get_block.callback) {
                client->pending_req.get_block.callback(
                        client,
                        client->pending_req.get_block.path,
                        data,
                        data_len,
                        client->pending_req.get_block.arg);
            }
        }
    }

    notify_observers(received, client, data, data_len);

    return COAP_RESPONSE_OK;
}

static int event_handler(coap_session_t* session, const coap_event_t event) {
    coap_context_t* coap_context = coap_session_get_context(session);
    golioth_coap_client_t* client = (golioth_coap_client_t*)coap_get_app_data(coap_context);

    ESP_LOGD(TAG, "event: 0x%04X", event);
    switch (event) {
        case COAP_EVENT_SESSION_CONNECTED:
            ESP_LOGI(TAG, "Session connected");
            if (client->event_callback && !client->session_connected) {
                client->event_callback(
                        client, GOLIOTH_CLIENT_EVENT_CONNECTED, client->event_callback_arg);
            }
            client->session_connected = true;
            break;
        case COAP_EVENT_DTLS_CONNECTED:
        case COAP_EVENT_DTLS_RENEGOTIATE:
        case COAP_EVENT_PARTIAL_BLOCK:
        case COAP_EVENT_TCP_CONNECTED:
            break;
        case COAP_EVENT_DTLS_CLOSED:
        case COAP_EVENT_TCP_CLOSED:
        case COAP_EVENT_SESSION_CLOSED:
        case COAP_EVENT_DTLS_ERROR:
        case COAP_EVENT_TCP_FAILED:
        case COAP_EVENT_SESSION_FAILED:
            ESP_LOGE(TAG, "Session error. Ending session.");
            if (client->event_callback && client->session_connected) {
                client->event_callback(
                        client, GOLIOTH_CLIENT_EVENT_DISCONNECTED, client->event_callback_arg);
            }
            client->session_connected = false;
            client->end_session = true;
            break;
        default:
            break;
    }
    return 0;
}

static void nack_handler(
        coap_session_t* session,
        const coap_pdu_t* sent,
        const coap_nack_reason_t reason,
        const coap_mid_t id) {
    coap_context_t* coap_context = coap_session_get_context(session);
    golioth_coap_client_t* client = (golioth_coap_client_t*)coap_get_app_data(coap_context);

    switch (reason) {
        case COAP_NACK_TOO_MANY_RETRIES:
        case COAP_NACK_NOT_DELIVERABLE:
        case COAP_NACK_RST:
        case COAP_NACK_TLS_FAILED:
        case COAP_NACK_ICMP_ISSUE:
            ESP_LOGE(TAG, "Received nack reason: %d. Ending session.", reason);
            if (client->event_callback && client->session_connected) {
                client->event_callback(
                        client, GOLIOTH_CLIENT_EVENT_DISCONNECTED, client->event_callback_arg);
            }
            client->session_connected = false;
            client->end_session = true;
            break;
        default:
            break;
    }
}

static void coap_log_handler(coap_log_t level, const char* message) {
    if (level <= LOG_ERR) {
        ESP_LOGE("libcoap", "%s", message);
    } else if (level <= LOG_WARNING) {
        ESP_LOGW("libcoap", "%s", message);
    } else if (level <= LOG_INFO) {
        ESP_LOGI("libcoap", "%s", message);
    } else {
        ESP_LOGD("libcoap", "%s", message);
    }
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
    // Token is stored in the client, so need to get a pointer to it from the context
    coap_context_t* coap_context = coap_session_get_context(session);
    golioth_coap_client_t* client = (golioth_coap_client_t*)coap_get_app_data(coap_context);

    coap_session_new_token(session, &client->token_len, client->token);
    coap_add_token(request, client->token_len, client->token);
}

static void golioth_coap_add_path(coap_pdu_t* request, const char* path_prefix, const char* path) {
    if (!path_prefix) {
        path_prefix = "";
    }
    assert(path);

    char fullpath[64] = {};
    snprintf(fullpath, sizeof(fullpath), "%s%s", path_prefix, path);

    size_t fullpathlen = strlen(fullpath);
    unsigned char buf[64];
    unsigned char* pbuf = buf;
    size_t buflen = sizeof(buf);
    int nsegments = coap_split_path((const uint8_t*)fullpath, fullpathlen, pbuf, &buflen);
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

static void golioth_coap_add_block2(coap_pdu_t* request, size_t block_index, size_t block_size) {
    size_t szx = 6;  // 1024 bytes
    coap_block_t block = {
            .num = block_index,
            .m = 0,
            .szx = szx,
    };

    unsigned char buf[4];
    unsigned int opt_length =
            coap_encode_var_safe(buf, sizeof(buf), (block.num << 4 | block.m << 3 | block.szx));
    coap_add_option(request, COAP_OPTION_BLOCK2, opt_length, buf);
}

static void golioth_coap_empty(coap_session_t* session) {
    // Note: libcoap has keepalive functionality built in, but we're not using because
    // it doesn't seem to work correctly. The server responds to the keepalive message,
    // but libcoap is disconnecting the session after the response is received:
    //
    //     DTLS: session disconnected (reason 1)
    //
    // Instead, we will send an empty DELETE request
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_DELETE, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() delete failed");
        return;
    }
    golioth_coap_add_token(request, session);
    coap_send(session, request);
}

static void golioth_coap_get(const golioth_coap_get_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path_prefix, params->path);
    golioth_coap_add_content_type(request, params->content_type);
    coap_send(session, request);
}

static void golioth_coap_get_block(
        golioth_coap_client_t* client,
        const golioth_coap_get_block_params_t* params,
        coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }

    if (params->block_index == 0) {
        golioth_coap_add_token(request, session);
        memcpy(client->block_token, client->token, client->token_len);
        client->block_token_len = client->token_len;
    } else {
        coap_add_token(request, client->block_token_len, client->block_token);

        // Copy block token into the current request token, since this is what
        // is checked in coap_response_handler to verify the response has been received.
        memcpy(client->token, client->block_token, client->block_token_len);
        client->token_len = client->block_token_len;
    }

    golioth_coap_add_path(request, params->path_prefix, params->path);
    golioth_coap_add_block2(request, params->block_index, params->block_size);
    coap_send(session, request);
}

static void golioth_coap_post(const golioth_coap_post_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_POST, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() post failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path_prefix, params->path);
    golioth_coap_add_content_type(request, params->content_type);
    coap_add_data(request, params->payload_size, (unsigned char*)params->payload);
    coap_send(session, request);
}

static void
golioth_coap_delete(const golioth_coap_delete_params_t* params, coap_session_t* session) {
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_DELETE, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() delete failed");
        return;
    }

    golioth_coap_add_token(request, session);
    golioth_coap_add_path(request, params->path_prefix, params->path);
    coap_send(session, request);
}

static void
add_observation(golioth_coap_client_t* client, const golioth_coap_observe_params_t* params) {
    // scan for available (not used) observation slot
    golioth_coap_observe_info_t* obs_info = NULL;
    bool found_slot = false;
    for (int i = 0; i < CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS; i++) {
        obs_info = &client->observations[i];
        if (!obs_info->in_use) {
            found_slot = true;
            break;
        }
    }

    if (!found_slot) {
        ESP_LOGE(TAG, "Unable to observe path %s, no slots available", params->path);
        return;
    }

    obs_info->in_use = true;
    obs_info->req_params = *params;
    memcpy(obs_info->token, client->token, client->token_len);
    obs_info->token_len = client->token_len;
}

static void golioth_coap_observe(
        golioth_coap_client_t* client,
        const golioth_coap_observe_params_t* params,
        coap_session_t* session) {
    // GET with an OBSERVE option
    coap_pdu_t* request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }

    golioth_coap_add_token(request, session);

    unsigned char optbuf[4] = {};
    coap_add_option(
            request,
            COAP_OPTION_OBSERVE,
            coap_encode_var_safe(optbuf, sizeof(optbuf), COAP_OBSERVE_ESTABLISH),
            optbuf);

    golioth_coap_add_path(request, params->path_prefix, params->path);
    golioth_coap_add_content_type(request, params->content_type);

    coap_send(session, request);
}

static void reestablish_observations(golioth_coap_client_t* client, coap_session_t* session) {
    golioth_coap_observe_info_t* obs_info = NULL;
    for (int i = 0; i < CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS; i++) {
        obs_info = &client->observations[i];
        if (obs_info->in_use) {
            golioth_coap_observe(client, &obs_info->req_params, session);
            memcpy(obs_info->token, client->token, client->token_len);
            obs_info->token_len = client->token_len;
        }
    }
}

static golioth_status_t create_context(golioth_coap_client_t* client, coap_context_t** context) {
    *context = coap_new_context(NULL);
    if (!*context) {
        ESP_LOGE(TAG, "Failed to create CoAP context");
        return GOLIOTH_ERR_MEM_ALLOC;
    }

    // Store our client pointer in the context, since it's needed in the reponse handler
    // we register below.
    coap_set_app_data(*context, client);

    // Register handlers
    coap_register_response_handler(*context, coap_response_handler);
    coap_register_event_handler(*context, event_handler);
    coap_register_nack_handler(*context, nack_handler);

    return GOLIOTH_OK;
}

static golioth_status_t
create_session(golioth_coap_client_t* client, coap_context_t* context, coap_session_t** session) {
    // Split URI for host
    coap_uri_t host_uri = {};
    int uri_status = coap_split_uri(
            (const uint8_t*)CONFIG_GOLIOTH_COAP_HOST_URI,
            strlen(CONFIG_GOLIOTH_COAP_HOST_URI),
            &host_uri);
    if (uri_status < 0) {
        ESP_LOGE(TAG, "CoAP host URI invalid: %s", CONFIG_GOLIOTH_COAP_HOST_URI);
        return GOLIOTH_ERR_INVALID_FORMAT;
    }

    // Get destination address of host
    coap_address_t dst_addr = {};
    GOLIOTH_STATUS_RETURN_IF_ERROR(get_coap_dst_address(&host_uri, &dst_addr));

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
    *session = coap_new_client_session_psk2(context, NULL, &dst_addr, COAP_PROTO_DTLS, &dtls_psk);
    if (!*session) {
        ESP_LOGE(TAG, "coap_new_client_session() failed");
        return GOLIOTH_ERR_MEM_ALLOC;
    }

    return GOLIOTH_OK;
}

static golioth_status_t
coap_io_loop_once(golioth_coap_client_t* client, coap_context_t* context, coap_session_t* session) {
    golioth_coap_request_msg_t request_msg = {};

    // Wait for request message, with timeout
    bool got_request_msg = xQueueReceive(
            client->request_queue,
            &request_msg,
            CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (!got_request_msg) {
        // No requests, so process other pending IO (e.g. observations)
        ESP_LOGD(TAG, "Idle io process start");
        coap_io_process(context, COAP_IO_NO_WAIT);
        ESP_LOGD(TAG, "Idle io process end");
        return GOLIOTH_OK;
    }

    // Handle message and send request to server
    bool request_is_valid = true;
    switch (request_msg.type) {
        case GOLIOTH_COAP_REQUEST_EMPTY:
            ESP_LOGD(TAG, "Handle EMPTY");
            golioth_coap_empty(session);
            break;
        case GOLIOTH_COAP_REQUEST_GET:
            ESP_LOGD(TAG, "Handle GET %s", request_msg.get.path);
            golioth_coap_get(&request_msg.get, session);
            break;
        case GOLIOTH_COAP_REQUEST_GET_BLOCK:
            ESP_LOGD(TAG, "Handle GET_BLOCK %s", request_msg.get_block.path);
            golioth_coap_get_block(client, &request_msg.get_block, session);
            break;
        case GOLIOTH_COAP_REQUEST_POST:
            ESP_LOGD(TAG, "Handle POST %s", request_msg.post.path);
            golioth_coap_post(&request_msg.post, session);
            assert(request_msg.post.payload);
            free(request_msg.post.payload);
            g_golioth_stats.total_freed_bytes += request_msg.post.payload_size;
            break;
        case GOLIOTH_COAP_REQUEST_DELETE:
            ESP_LOGD(TAG, "Handle DELETE %s", request_msg.delete.path);
            golioth_coap_delete(&request_msg.delete, session);
            break;
        case GOLIOTH_COAP_REQUEST_OBSERVE:
            ESP_LOGD(TAG, "Handle OBSERVE %s", request_msg.observe.path);
            golioth_coap_observe(client, &request_msg.observe, session);
            add_observation(client, &request_msg.observe);
            break;
        default:
            ESP_LOGW(TAG, "Unknown request_msg type: %u", request_msg.type);
            request_is_valid = false;
            break;
    }
    if (!request_is_valid) {
        return GOLIOTH_OK;
    }


    // If we get here, then a confirmable request has been sent to the server,
    // and we should wait for a response.
    client->pending_req = request_msg;
    client->got_coap_response = false;
    int32_t time_spent_waiting_ms = 0;
    int32_t timeout_ms = CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_S * 1000;
    bool io_error = false;
    while (time_spent_waiting_ms < timeout_ms) {
        int32_t remaining_ms = timeout_ms - time_spent_waiting_ms;
        int32_t wait_ms = min(1000, remaining_ms);
        ESP_LOGD(TAG, "Response wait io process start");
        int32_t num_ms = coap_io_process(context, wait_ms);
        ESP_LOGD(TAG, "Response wait io process end");
        if (num_ms < 0) {
            io_error = true;
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

    if (request_msg.request_complete_sem) {
        xSemaphoreGive(request_msg.request_complete_sem);
    }

    if (io_error) {
        ESP_LOGE(TAG, "Error in coap_io_process");
        return GOLIOTH_ERR_IO;
    }

    if (time_spent_waiting_ms >= timeout_ms) {
        ESP_LOGE(TAG, "Timeout: never got a response from the server");
        if (client->event_callback && client->session_connected) {
            client->event_callback(
                    client, GOLIOTH_CLIENT_EVENT_DISCONNECTED, client->event_callback_arg);
        }
        client->session_connected = false;
        return GOLIOTH_ERR_TIMEOUT;
    }

    if (client->event_callback && !client->session_connected) {
        client->event_callback(client, GOLIOTH_CLIENT_EVENT_CONNECTED, client->event_callback_arg);
    }
    client->session_connected = true;
    return GOLIOTH_OK;
}

static void on_keepalive(TimerHandle_t timer) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)pvTimerGetTimerID(timer);
    ESP_LOGD(TAG, "keepalive");
    c->keepalive_count++;
    golioth_coap_client_empty(c, false);
}

// Note: libcoap is not thread safe, so all rx/tx I/O for the session must be
// done in this task.
static void golioth_coap_client_task(void* arg) {
    golioth_coap_client_t* client = (golioth_coap_client_t*)arg;

    while (1) {
        coap_context_t* coap_context = NULL;
        coap_session_t* coap_session = NULL;

        client->end_session = false;
        client->session_connected = false;

        ESP_LOGD(TAG, "Waiting for the \"run\" signal");
        xSemaphoreTake(client->run_sem, portMAX_DELAY);
        xSemaphoreGive(client->run_sem);
        ESP_LOGD(TAG, "Received \"run\" signal");

        if (create_context(client, &coap_context) != GOLIOTH_OK) {
            goto cleanup;
        }
        if (create_session(client, coap_context, &coap_session) != GOLIOTH_OK) {
            goto cleanup;
        }

        // Seed the session token generator
        uint8_t seed_token[8];
        size_t seed_token_len;
        uint32_t randint = (uint32_t)rand();
        seed_token_len = coap_encode_var_safe8(seed_token, sizeof(seed_token), randint);
        coap_session_init_token(coap_session, seed_token_len, seed_token);

        // Enqueue an asynchronous EMPTY request immediately.
        //
        // This is done so we can determine quickly whether we are connected
        // to the cloud or not (libcoap does not tell us when it's connected
        // for some reason, so this is a workaround for that).
        golioth_coap_empty(coap_session);

        // If we are re-connecting and had prior observations, set
        // them up again now (tokens will be updated).
        reestablish_observations(client, coap_session);

        ESP_LOGI(TAG, "Entering CoAP I/O loop");
        int iteration = 0;
        while (!client->end_session) {
            // Check if we should still run (non-blocking)
            if (!xSemaphoreTake(client->run_sem, 0)) {
                ESP_LOGI(TAG, "Stopping");
                break;
            }
            xSemaphoreGive(client->run_sem);

            if (coap_io_loop_once(client, coap_context, coap_session) != GOLIOTH_OK) {
                client->end_session = true;
            }
            iteration++;
        }

    cleanup:
        ESP_LOGI(TAG, "Ending session");
        if (coap_session) {
            coap_session_release(coap_session);
        }
        if (coap_context) {
            coap_free_context(coap_context);
        }
        coap_cleanup();

        ESP_LOGI(TAG, "Delay before re-starting session");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

golioth_client_t golioth_client_create(const char* psk_id, const char* psk) {
    if (!_initialized) {
        // Connect logs from libcoap to the ESP logger
        coap_set_log_handler(coap_log_handler);
        coap_set_log_level(6);  // 3: error, 4: warning, 6: info, 7: debug, 9:mbedtls

        // Seed the random number generator. Used for token generation.
        time_t t;
        srand(time(&t));

        _initialized = true;
    }

    golioth_coap_client_t* new_client = calloc(1, sizeof(golioth_coap_client_t));
    if (!new_client) {
        ESP_LOGE(TAG, "Failed to allocate memory for client");
        goto error;
    }
    g_golioth_stats.total_allocd_bytes += sizeof(golioth_coap_client_t);

    new_client->psk_id = psk_id;
    new_client->psk_id_len = strlen(psk_id);
    new_client->psk = psk;
    new_client->psk_len = strlen(psk);

    new_client->run_sem = xSemaphoreCreateBinary();
    if (!new_client->run_sem) {
        ESP_LOGE(TAG, "Failed to create run semaphore");
        goto error;
    }
    xSemaphoreGive(new_client->run_sem);

    new_client->request_queue = xQueueCreate(
            CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_MAX_ITEMS, sizeof(golioth_coap_request_msg_t));
    if (!new_client->request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        goto error;
    }

    bool task_created = xTaskCreate(
            golioth_coap_client_task,
            "coap_client",
            CONFIG_GOLIOTH_COAP_TASK_STACK_SIZE_BYTES,
            new_client,  // task arg
            CONFIG_GOLIOTH_COAP_TASK_PRIORITY,
            &new_client->coap_task_handle);
    if (!task_created) {
        ESP_LOGE(TAG, "Failed to create client task");
        goto error;
    }

    new_client->keepalive_timer = xTimerCreate(
            "keepalive",
            max(1000, 1000 * CONFIG_GOLIOTH_COAP_KEEPALIVE_INTERVAL_S) / portTICK_PERIOD_MS,
            pdTRUE,      // auto-reload
            new_client,  // pvTimerID
            on_keepalive);
    if (!new_client->keepalive_timer) {
        ESP_LOGE(TAG, "Failed to create keepalive timer");
        goto error;
    }

    if (CONFIG_GOLIOTH_COAP_KEEPALIVE_INTERVAL_S > 0) {
        if (!xTimerStart(new_client->keepalive_timer, 0)) {
            ESP_LOGE(TAG, "Failed to start keepalive timer");
            goto error;
        }
    }

    return (golioth_client_t)new_client;

error:
    golioth_client_destroy(new_client);
    return NULL;
}

golioth_status_t golioth_client_start(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }
    xSemaphoreGive(c->run_sem);
    return GOLIOTH_OK;
}

golioth_status_t golioth_client_stop(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }
    if (!xSemaphoreTake(c->run_sem, 100 / portTICK_PERIOD_MS)) {
        ESP_LOGE(TAG, "stop: failed to take run_sem");
        return GOLIOTH_ERR_TIMEOUT;
    }
    return GOLIOTH_OK;
}

void golioth_client_destroy(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return;
    }
    if (c->keepalive_timer) {
        xTimerDelete(c->keepalive_timer, 0);
    }
    if (c->coap_task_handle) {
        vTaskDelete(c->coap_task_handle);
    }
    // TODO: purge queue, free dyn mem for requests that have it
    if (c->request_queue) {
        vQueueDelete(c->request_queue);
    }
    if (c->run_sem) {
        vSemaphoreDelete(c->run_sem);
    }
    free(c);
    g_golioth_stats.total_freed_bytes += sizeof(golioth_coap_client_t);
}

bool golioth_client_is_connected(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return false;
    }
    return c->session_connected;
}

golioth_status_t golioth_coap_client_empty(golioth_client_t client, bool is_synchronous) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    golioth_status_t ret = GOLIOTH_OK;
    SemaphoreHandle_t request_complete_sem = NULL;

    if (is_synchronous) {
        request_complete_sem = xSemaphoreCreateBinary();
        if (!request_complete_sem) {
            ESP_LOGE(TAG, "Failed to create request_complete semaphore");
            ret = GOLIOTH_ERR_MEM_ALLOC;
            goto cleanup;
        }
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_EMPTY,
            .request_complete_sem = request_complete_sem,
    };

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        ret = GOLIOTH_ERR_QUEUE_FULL;
        goto cleanup;
    }

    if (is_synchronous) {
        xSemaphoreTake(request_complete_sem, portMAX_DELAY);
    }

cleanup:
    if (request_complete_sem) {
        vSemaphoreDelete(request_complete_sem);
    }
    return ret;
}

golioth_status_t golioth_coap_client_set(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        const uint8_t* payload,
        size_t payload_size,
        bool is_synchronous) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    golioth_status_t ret = GOLIOTH_OK;
    SemaphoreHandle_t request_complete_sem = NULL;
    uint8_t* request_payload = NULL;

    if (payload_size > 0) {
        // We will allocate memory and copy the payload
        // to avoid payload lifetime and thread-safety issues.
        //
        // This memory will be free'd by the CoAP task after handling the request,
        // or in this function if we fail to enqueue the request.
        request_payload = (uint8_t*)calloc(1, payload_size);
        if (!request_payload) {
            ESP_LOGE(TAG, "Payload alloc failure");
            ret = GOLIOTH_ERR_MEM_ALLOC;
            goto cleanup;
        }
        g_golioth_stats.total_allocd_bytes += payload_size;
        memcpy(request_payload, payload, payload_size);
    }

    if (is_synchronous) {
        request_complete_sem = xSemaphoreCreateBinary();
        if (!request_complete_sem) {
            ESP_LOGE(TAG, "Failed to create request_complete semaphore");
            ret = GOLIOTH_ERR_MEM_ALLOC;
            goto cleanup;
        }
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_POST,
            .post =
                    {
                            .path_prefix = path_prefix,
                            .path = path,
                            .content_type = content_type,
                            .payload = request_payload,
                            .payload_size = payload_size,
                    },
            .request_complete_sem = request_complete_sem,
    };

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        ret = GOLIOTH_ERR_QUEUE_FULL;
        goto cleanup;
    }

    if (is_synchronous) {
        xSemaphoreTake(request_complete_sem, portMAX_DELAY);
    }

cleanup:
    if (request_complete_sem) {
        vSemaphoreDelete(request_complete_sem);
    }
    if (ret != GOLIOTH_OK) {
        // Failed to enqueue, free the payload copy
        if (payload_size > 0) {
            free(request_payload);
            g_golioth_stats.total_freed_bytes += payload_size;
        }
    }
    return ret;
}

golioth_status_t golioth_coap_client_delete(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        bool is_synchronous) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    golioth_status_t ret = GOLIOTH_OK;
    SemaphoreHandle_t request_complete_sem = NULL;

    if (is_synchronous) {
        request_complete_sem = xSemaphoreCreateBinary();
        if (!request_complete_sem) {
            ESP_LOGE(TAG, "Failed to create request_complete semaphore");
            ret = GOLIOTH_ERR_MEM_ALLOC;
            goto cleanup;
        }
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_DELETE,
            .delete =
                    {
                            .path_prefix = path_prefix,
                            .path = path,
                    },
            .request_complete_sem = request_complete_sem,
    };

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGE(TAG, "Failed to enqueue request, queue full");
        ret = GOLIOTH_ERR_QUEUE_FULL;
        goto cleanup;
    }

    if (is_synchronous) {
        xSemaphoreTake(request_complete_sem, portMAX_DELAY);
    }

cleanup:
    if (request_complete_sem) {
        vSemaphoreDelete(request_complete_sem);
    }
    return ret;
}

static golioth_status_t golioth_coap_client_get_internal(
        golioth_client_t client,
        golioth_coap_request_type_t type,
        void* request_params,
        bool is_synchronous) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    golioth_status_t ret = GOLIOTH_OK;
    SemaphoreHandle_t request_complete_sem = NULL;

    if (is_synchronous) {
        request_complete_sem = xSemaphoreCreateBinary();
        if (!request_complete_sem) {
            ESP_LOGE(TAG, "Failed to create request_complete semaphore");
            ret = GOLIOTH_ERR_MEM_ALLOC;
            goto cleanup;
        }
    }

    golioth_coap_request_msg_t request_msg = {};
    request_msg.type = type;
    request_msg.request_complete_sem = request_complete_sem;
    if (type == GOLIOTH_COAP_REQUEST_GET_BLOCK) {
        request_msg.get_block = *(golioth_coap_get_block_params_t*)request_params;
    } else {
        assert(type == GOLIOTH_COAP_REQUEST_GET);
        request_msg.get = *(golioth_coap_get_params_t*)request_params;
    }

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGE(TAG, "Failed to enqueue request, queue full");
        ret = GOLIOTH_ERR_QUEUE_FULL;
        goto cleanup;
    }

    if (is_synchronous) {
        xSemaphoreTake(request_complete_sem, portMAX_DELAY);
    }

cleanup:
    if (request_complete_sem) {
        vSemaphoreDelete(request_complete_sem);
    }
    return ret;
}

golioth_status_t golioth_coap_client_get(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        golioth_get_cb_fn callback,
        void* arg,
        bool is_synchronous) {
    golioth_coap_get_params_t params = {
            .path_prefix = path_prefix,
            .path = path,
            .content_type = content_type,
            .callback = callback,
            .arg = arg,
    };
    return golioth_coap_client_get_internal(
            client, GOLIOTH_COAP_REQUEST_GET, &params, is_synchronous);
}

golioth_status_t golioth_coap_client_get_block(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        size_t block_index,
        size_t block_size,
        golioth_get_cb_fn callback,
        void* arg,
        bool is_synchronous) {
    golioth_coap_get_block_params_t params = {
            .path_prefix = path_prefix,
            .path = path,
            .content_type = content_type,
            .block_index = block_index,
            .block_size = block_size,
            .callback = callback,
            .arg = arg,
    };
    return golioth_coap_client_get_internal(
            client, GOLIOTH_COAP_REQUEST_GET_BLOCK, &params, is_synchronous);
}

golioth_status_t golioth_coap_client_observe_async(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        golioth_get_cb_fn callback,
        void* arg) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_OBSERVE,
            .observe =
                    {
                            .path_prefix = path_prefix,
                            .path = path,
                            .content_type = content_type,
                            .callback = callback,
                            .arg = arg,
                    },
    };
    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    return GOLIOTH_OK;
}

void golioth_client_register_event_callback(
        golioth_client_t client,
        golioth_client_event_cb_fn callback,
        void* arg) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return;
    }
    c->event_callback = callback;
    c->event_callback_arg = arg;
}
