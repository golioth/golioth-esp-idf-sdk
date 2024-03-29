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
#include "golioth_statistics.h"
#include "golioth_util.h"
#include "golioth_time.h"
#include "golioth_lightdb.h"

#define TAG "golioth_coap_client"

static bool _initialized;

// This is the struct hidden by the opaque type golioth_client_t
// TODO - document these
typedef struct {
    QueueHandle_t request_queue;
    TaskHandle_t coap_task_handle;
    SemaphoreHandle_t run_sem;
    TimerHandle_t keepalive_timer;
    bool is_running;
    bool end_session;
    bool session_connected;
    golioth_client_config_t config;
    const char* psk;
    size_t psk_len;
    golioth_coap_request_msg_t* pending_req;
    golioth_coap_observe_info_t observations[CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS];
    // token to use for block GETs (must use same token for all blocks)
    uint8_t block_token[8];
    size_t block_token_len;
    golioth_client_event_cb_fn event_callback;
    void* event_callback_arg;
} golioth_coap_client_t;

static bool token_matches_request(
        const golioth_coap_request_msg_t* req,
        const coap_pdu_t* received) {
    coap_bin_const_t rcvd_token = coap_pdu_get_token(received);
    bool len_matches = (rcvd_token.length == req->token_len);
    return (len_matches && (0 == memcmp(rcvd_token.s, req->token, req->token_len)));
}

static void notify_observers(
        const coap_pdu_t* received,
        golioth_coap_client_t* client,
        const uint8_t* data,
        size_t data_len,
        const golioth_response_t* response) {
    // scan observations, check for token match
    for (int i = 0; i < CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS; i++) {
        const golioth_coap_observe_info_t* obs_info = &client->observations[i];
        golioth_get_cb_fn callback = obs_info->req.observe.callback;

        if (!obs_info->in_use || !callback) {
            continue;
        }

        coap_bin_const_t rcvd_token = coap_pdu_get_token(received);
        bool len_matches = (rcvd_token.length == obs_info->req.token_len);
        if (len_matches
            && (0 == memcmp(rcvd_token.s, obs_info->req.token, obs_info->req.token_len))) {
            callback(
                    client,
                    response,
                    obs_info->req.path,
                    data,
                    data_len,
                    obs_info->req.observe.arg);
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
    uint8_t class = rcvd_code >> 5;
    uint8_t code = rcvd_code & 0x1F;

    if (rcv_type == COAP_MESSAGE_RST) {
        ESP_LOGW(TAG, "Got RST");
        return COAP_RESPONSE_OK;
    }

    golioth_response_t response = {
            .status = (class == 2 ? GOLIOTH_OK : GOLIOTH_ERR_FAIL),
            .class = class,
            .code = code,
    };

    assert(session);
    coap_context_t* coap_context = coap_session_get_context(session);
    assert(coap_context);
    golioth_coap_client_t* client = (golioth_coap_client_t*)coap_get_app_data(coap_context);
    assert(client);

    const uint8_t* data = NULL;
    size_t data_len = 0;
    coap_get_data(received, &data_len, &data);

    // Get the original/pending request info
    golioth_coap_request_msg_t* req = client->pending_req;

    if (req) {
        if (req->type == GOLIOTH_COAP_REQUEST_EMPTY) {
            ESP_LOGD(TAG, "%d.%02d (empty req), len %zu", class, code, data_len);
        } else if (class != 2) {  // not 2.XX, i.e. not success
            ESP_LOGW(
                    TAG,
                    "%d.%02d (req type: %d, path: %s%s), len %zu",
                    class,
                    code,
                    req->type,
                    req->path_prefix,
                    req->path,
                    data_len);
        } else {
            ESP_LOGD(
                    TAG,
                    "%d.%02d (req type: %d, path: %s%s), len %zu",
                    class,
                    code,
                    req->type,
                    req->path_prefix,
                    req->path,
                    data_len);
        }
    } else {
        ESP_LOGD(TAG, "%d.%02d (unsolicited), len %zu", class, code, data_len);
    }

    if (req && token_matches_request(req, received)) {
        req->got_response = true;

        if (CONFIG_GOLIOTH_COAP_KEEPALIVE_INTERVAL_S > 0) {
            if (!xTimerReset(client->keepalive_timer, 0)) {
                ESP_LOGW(TAG, "Failed to reset keepalive timer");
            }
        }

        if (golioth_time_millis() > req->ageout_ms) {
            ESP_LOGW(TAG, "Ignoring response from old request, type %d", req->type);
        } else {
            if (req->type == GOLIOTH_COAP_REQUEST_GET) {
                if (req->get.callback) {
                    req->get.callback(client, &response, req->path, data, data_len, req->get.arg);
                }
            } else if (req->type == GOLIOTH_COAP_REQUEST_GET_BLOCK) {
                coap_opt_iterator_t opt_iter;
                coap_opt_t* block_opt = coap_check_option(received, COAP_OPTION_BLOCK2, &opt_iter);
                assert(block_opt);
                uint32_t opt_block_index = coap_opt_block_num(block_opt);

                ESP_LOGD(
                        TAG,
                        "Request block index = %u, response block index = %u, offset 0x%08X",
                        req->get_block.block_index,
                        opt_block_index,
                        opt_block_index * 1024);
                ESP_LOG_BUFFER_HEXDUMP(TAG, data, min(32, data_len), ESP_LOG_DEBUG);

                if (req->get_block.callback) {
                    req->get_block.callback(
                            client, &response, req->path, data, data_len, req->get_block.arg);
                }
            } else if (req->type == GOLIOTH_COAP_REQUEST_POST) {
                if (req->post.callback) {
                    req->post.callback(client, &response, req->path, req->post.arg);
                }
            } else if (req->type == GOLIOTH_COAP_REQUEST_DELETE) {
                if (req->delete.callback) {
                    req->delete.callback(client, &response, req->path, req->delete.arg);
                }
            }
        }
    }

    notify_observers(received, client, data, data_len, &response);

    return COAP_RESPONSE_OK;
}

static int event_handler(coap_session_t* session, const coap_event_t event) {
    ESP_LOGD(TAG, "event: 0x%04X", event);
    return 0;
}

static void nack_handler(
        coap_session_t* session,
        const coap_pdu_t* sent,
        const coap_nack_reason_t reason,
        const coap_mid_t id) {
    switch (reason) {
        case COAP_NACK_TOO_MANY_RETRIES:
            ESP_LOGE(TAG, "Received nack reason: COAP_NACK_TOO_MANY_RETRIES");
            break;
        case COAP_NACK_NOT_DELIVERABLE:
            ESP_LOGE(TAG, "Received nack reason: COAP_NACK_NOT_DELIVERABLE");
            break;
        case COAP_NACK_TLS_FAILED:
            ESP_LOGE(TAG, "Received nack reason: COAP_NACK_TLS_FAILED");
            // TODO - customize error message based on PSK vs cert usage
            ESP_LOGE(TAG, "Maybe your PSK-ID or PSK is incorrect?");
            break;
        default:
            ESP_LOGE(TAG, "Received nack reason: %d", reason);
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
    GSTATS_INC_ALLOC("ainfo");

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
            GSTATS_INC_FREE("ainfo");
            return GOLIOTH_ERR_DNS_LOOKUP;
    }
    freeaddrinfo(ainfo);
    GSTATS_INC_FREE("ainfo");

    return GOLIOTH_OK;
}

static void golioth_coap_add_token(
        coap_pdu_t* req_pdu,
        golioth_coap_request_msg_t* req,
        coap_session_t* session) {
    coap_session_new_token(session, &req->token_len, req->token);
    coap_add_token(req_pdu, req->token_len, req->token);
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

static void golioth_coap_empty(golioth_coap_request_msg_t* req, coap_session_t* session) {
    // Note: libcoap has keepalive functionality built in, but we're not using because
    // it doesn't seem to work correctly. The server responds to the keepalive message,
    // but libcoap is disconnecting the session after the response is received:
    //
    //     DTLS: session disconnected (reason 1)
    //
    // Instead, we will send an empty DELETE request
    coap_pdu_t* req_pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_DELETE, session);
    if (!req_pdu) {
        ESP_LOGE(TAG, "coap_new_pdu() delete failed");
        return;
    }
    GSTATS_INC_ALLOC("empty_pdu");

    golioth_coap_add_token(req_pdu, req, session);
    coap_send(session, req_pdu);
    GSTATS_INC_FREE("empty_pdu");
}

static void golioth_coap_get(golioth_coap_request_msg_t* req, coap_session_t* session) {
    coap_pdu_t* req_pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!req_pdu) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }
    GSTATS_INC_ALLOC("get_pdu");

    golioth_coap_add_token(req_pdu, req, session);
    golioth_coap_add_path(req_pdu, req->path_prefix, req->path);
    golioth_coap_add_content_type(req_pdu, req->get.content_type);
    coap_send(session, req_pdu);
    GSTATS_INC_FREE("get_pdu");
}

static void golioth_coap_get_block(
        golioth_coap_request_msg_t* req,
        golioth_coap_client_t* client,
        coap_session_t* session) {
    coap_pdu_t* req_pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!req_pdu) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }
    GSTATS_INC_ALLOC("get_block_pdu");

    if (req->get_block.block_index == 0) {
        // Save this token for further blocks
        golioth_coap_add_token(req_pdu, req, session);
        memcpy(client->block_token, req->token, req->token_len);
        client->block_token_len = req->token_len;
    } else {
        coap_add_token(req_pdu, client->block_token_len, client->block_token);

        // Copy block token into the current req_pdu token, since this is what
        // is checked in coap_response_handler to verify the response has been received.
        memcpy(req->token, client->block_token, client->block_token_len);
        req->token_len = client->block_token_len;
    }

    golioth_coap_add_path(req_pdu, req->path_prefix, req->path);
    golioth_coap_add_block2(req_pdu, req->get_block.block_index, req->get_block.block_size);
    coap_send(session, req_pdu);
    GSTATS_INC_FREE("get_block_pdu");
}

static void golioth_coap_post(golioth_coap_request_msg_t* req, coap_session_t* session) {
    coap_pdu_t* req_pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_POST, session);
    if (!req_pdu) {
        ESP_LOGE(TAG, "coap_new_pdu() post failed");
        return;
    }
    GSTATS_INC_ALLOC("post_pdu");

    golioth_coap_add_token(req_pdu, req, session);
    golioth_coap_add_path(req_pdu, req->path_prefix, req->path);
    golioth_coap_add_content_type(req_pdu, req->post.content_type);
    coap_add_data(req_pdu, req->post.payload_size, (unsigned char*)req->post.payload);
    coap_send(session, req_pdu);
    GSTATS_INC_FREE("post_pdu");
}

static void golioth_coap_delete(golioth_coap_request_msg_t* req, coap_session_t* session) {
    coap_pdu_t* req_pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_DELETE, session);
    if (!req_pdu) {
        ESP_LOGE(TAG, "coap_new_pdu() delete failed");
        return;
    }
    GSTATS_INC_ALLOC("delete_pdu");

    golioth_coap_add_token(req_pdu, req, session);
    golioth_coap_add_path(req_pdu, req->path_prefix, req->path);
    coap_send(session, req_pdu);
    GSTATS_INC_FREE("delete_pdu");
}

static void add_observation(golioth_coap_request_msg_t* req, golioth_coap_client_t* client) {
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
        ESP_LOGE(TAG, "Unable to observe path %s, no slots available", req->path);
        return;
    }

    obs_info->in_use = true;
    memcpy(&obs_info->req, req, sizeof(obs_info->req));
}

static void golioth_coap_observe(
        golioth_coap_request_msg_t* req,
        golioth_coap_client_t* client,
        coap_session_t* session) {
    // GET with an OBSERVE option
    coap_pdu_t* req_pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, session);
    if (!req_pdu) {
        ESP_LOGE(TAG, "coap_new_pdu() get failed");
        return;
    }
    GSTATS_INC_ALLOC("observe_pdu");

    golioth_coap_add_token(req_pdu, req, session);

    unsigned char optbuf[4] = {};
    coap_add_option(
            req_pdu,
            COAP_OPTION_OBSERVE,
            coap_encode_var_safe(optbuf, sizeof(optbuf), COAP_OBSERVE_ESTABLISH),
            optbuf);

    golioth_coap_add_path(req_pdu, req->path_prefix, req->path);
    golioth_coap_add_content_type(req_pdu, req->observe.content_type);

    coap_send(session, req_pdu);
    GSTATS_INC_FREE("observe_pdu");
}

static void reestablish_observations(golioth_coap_client_t* client, coap_session_t* session) {
    golioth_coap_observe_info_t* obs_info = NULL;
    for (int i = 0; i < CONFIG_GOLIOTH_MAX_NUM_OBSERVATIONS; i++) {
        obs_info = &client->observations[i];
        if (obs_info->in_use) {
            golioth_coap_observe(&obs_info->req, client, session);
        }
    }
}

static golioth_status_t create_context(golioth_coap_client_t* client, coap_context_t** context) {
    *context = coap_new_context(NULL);
    if (!*context) {
        ESP_LOGE(TAG, "Failed to create CoAP context");
        return GOLIOTH_ERR_MEM_ALLOC;
    }
    GSTATS_INC_ALLOC("context");

    // Store our client pointer in the context, since it's needed in the reponse handler
    // we register below.
    coap_set_app_data(*context, client);

    // Register handlers
    coap_register_response_handler(*context, coap_response_handler);
    coap_register_event_handler(*context, event_handler);
    coap_register_nack_handler(*context, nack_handler);

    return GOLIOTH_OK;
}

static int validate_cn_call_back(
        const char* cn,
        const uint8_t* asn1_public_cert,
        size_t asn1_length,
        coap_session_t* session,
        unsigned depth,
        int validated,
        void* arg) {
    ESP_LOGI(TAG, "Server Cert: Depth = %u, Len = %zu, Valid = %d", depth, asn1_length, validated);
    return 1;
}

static golioth_status_t create_session(
        golioth_coap_client_t* client,
        coap_context_t* context,
        coap_session_t** session) {
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

    ESP_LOGI(TAG, "Start CoAP session with host: %s", CONFIG_GOLIOTH_COAP_HOST_URI);

    char client_sni[256] = {};
    memcpy(client_sni, host_uri.host.s, MIN(host_uri.host.length, sizeof(client_sni) - 1));

    golioth_tls_auth_type_t auth_type = client->config.credentials.auth_type;

    if (auth_type == GOLIOTH_TLS_AUTH_TYPE_PSK) {
        golioth_psk_credentials_t psk_creds = client->config.credentials.psk;

        coap_dtls_cpsk_t dtls_psk = {
                .version = COAP_DTLS_CPSK_SETUP_VERSION,
                .client_sni = client_sni,
                .psk_info.identity.s = (const uint8_t*)psk_creds.psk_id,
                .psk_info.identity.length = psk_creds.psk_id_len,
                .psk_info.key.s = (const uint8_t*)psk_creds.psk,
                .psk_info.key.length = psk_creds.psk_len,
        };
        *session =
                coap_new_client_session_psk2(context, NULL, &dst_addr, COAP_PROTO_DTLS, &dtls_psk);
    } else if (auth_type == GOLIOTH_TLS_AUTH_TYPE_PKI) {
        golioth_pki_credentials_t pki_creds = client->config.credentials.pki;

        coap_dtls_pki_t dtls_pki = {
                .version = COAP_DTLS_PKI_SETUP_VERSION,
                .verify_peer_cert = 1,
                .check_common_ca = 1,
                .allow_self_signed = 0,
                .allow_expired_certs = 0,
                .cert_chain_validation = 1,
                .cert_chain_verify_depth = 3,
                .check_cert_revocation = 1,
                .allow_no_crl = 1,
                .allow_expired_crl = 0,
                .allow_bad_md_hash = 0,
                .allow_short_rsa_length = 1,
                .is_rpk_not_cert = 0,
                .validate_cn_call_back = validate_cn_call_back,
                .client_sni = client_sni,
                .pki_key = {
                        .key_type = COAP_PKI_KEY_PEM_BUF,
                        .key.pem_buf = {
                                .ca_cert = pki_creds.ca_cert,
                                .ca_cert_len = pki_creds.ca_cert_len,
                                .public_cert = pki_creds.public_cert,
                                .public_cert_len = pki_creds.public_cert_len,
                                .private_key = pki_creds.private_key,
                                .private_key_len = pki_creds.private_key_len,
                        }}};
        *session =
                coap_new_client_session_pki(context, NULL, &dst_addr, COAP_PROTO_DTLS, &dtls_pki);
    } else {
        ESP_LOGE(TAG, "Invalid TLS auth type: %d", auth_type);
        return GOLIOTH_ERR_NOT_ALLOWED;
    }

    if (!*session) {
        ESP_LOGE(TAG, "coap_new_client_session() failed");
        return GOLIOTH_ERR_MEM_ALLOC;
    }
    GSTATS_INC_ALLOC("session");

    return GOLIOTH_OK;
}

static golioth_status_t coap_io_loop_once(
        golioth_coap_client_t* client,
        coap_context_t* context,
        coap_session_t* session) {
    golioth_coap_request_msg_t request_msg = {};

    // Wait for request message, with timeout
    bool got_request_msg = xQueueReceive(
            client->request_queue,
            &request_msg,
            CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (!got_request_msg) {
        // No requests, so process other pending IO (e.g. observations)
        ESP_LOGV(TAG, "Idle io process start");
        coap_io_process(context, COAP_IO_NO_WAIT);
        ESP_LOGV(TAG, "Idle io process end");
        return GOLIOTH_OK;
    }

    // Make sure the request isn't too old
    if (golioth_time_millis() > request_msg.ageout_ms) {
        ESP_LOGW(
                TAG,
                "Ignoring request that has aged out, type %d, path %s",
                request_msg.type,
                (request_msg.path ? request_msg.path : "N/A"));

        if (request_msg.type == GOLIOTH_COAP_REQUEST_POST && request_msg.post.payload_size > 0) {
            free(request_msg.post.payload);
            GSTATS_INC_FREE("request_payload");
        }

        if (request_msg.request_complete_event) {
            assert(request_msg.request_complete_ack_sem);
            vEventGroupDelete(request_msg.request_complete_event);
            GSTATS_INC_FREE("request_complete_event");
            vSemaphoreDelete(request_msg.request_complete_ack_sem);
            GSTATS_INC_FREE("request_complete_ack_sem");
        }
        return GOLIOTH_OK;
    }

    // Handle message and send request to server
    bool request_is_valid = true;
    switch (request_msg.type) {
        case GOLIOTH_COAP_REQUEST_EMPTY:
            ESP_LOGD(TAG, "Handle EMPTY");
            golioth_coap_empty(&request_msg, session);
            break;
        case GOLIOTH_COAP_REQUEST_GET:
            ESP_LOGD(TAG, "Handle GET %s", request_msg.path);
            golioth_coap_get(&request_msg, session);
            break;
        case GOLIOTH_COAP_REQUEST_GET_BLOCK:
            ESP_LOGD(TAG, "Handle GET_BLOCK %s", request_msg.path);
            golioth_coap_get_block(&request_msg, client, session);
            break;
        case GOLIOTH_COAP_REQUEST_POST:
            ESP_LOGD(TAG, "Handle POST %s", request_msg.path);
            golioth_coap_post(&request_msg, session);
            assert(request_msg.post.payload);
            free(request_msg.post.payload);
            GSTATS_INC_FREE("request_payload");
            break;
        case GOLIOTH_COAP_REQUEST_DELETE:
            ESP_LOGD(TAG, "Handle DELETE %s", request_msg.path);
            golioth_coap_delete(&request_msg, session);
            break;
        case GOLIOTH_COAP_REQUEST_OBSERVE:
            ESP_LOGD(TAG, "Handle OBSERVE %s", request_msg.path);
            golioth_coap_observe(&request_msg, client, session);
            add_observation(&request_msg, client);
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
    client->pending_req = &request_msg;
    request_msg.got_response = false;
    int32_t time_spent_waiting_ms = 0;
    int32_t timeout_ms = CONFIG_GOLIOTH_COAP_RESPONSE_TIMEOUT_S * 1000;

    if (request_msg.ageout_ms != GOLIOTH_WAIT_FOREVER) {
        int32_t time_till_ageout_ms = (int32_t)(request_msg.ageout_ms - golioth_time_millis());
        timeout_ms = min(timeout_ms, time_till_ageout_ms);
    }

    bool io_error = false;
    while (time_spent_waiting_ms < timeout_ms) {
        int32_t remaining_ms = timeout_ms - time_spent_waiting_ms;
        int32_t wait_ms = min(1000, remaining_ms);
        int32_t num_ms = coap_io_process(context, wait_ms);
        if (num_ms < 0) {
            io_error = true;
            break;
        } else {
            time_spent_waiting_ms += num_ms;
            if (request_msg.got_response) {
                ESP_LOGD(TAG, "Received response in %d ms", time_spent_waiting_ms);
                break;
            } else {
                // During normal operation, there will be other kinds of IO to process,
                // in which case we will get here.
                // Since we haven't received the response yet, just keep waiting.
            }
        }
    }
    client->pending_req = NULL;

    if (request_msg.request_complete_event) {
        assert(request_msg.request_complete_ack_sem);

        if (request_msg.got_response) {
            xEventGroupSetBits(request_msg.request_complete_event, RESPONSE_RECEIVED_EVENT_BIT);
        } else {
            xEventGroupSetBits(request_msg.request_complete_event, RESPONSE_TIMEOUT_EVENT_BIT);
        }

        // Wait for user task to receive the event.
        xSemaphoreTake(request_msg.request_complete_ack_sem, portMAX_DELAY);

        // Now it's safe to delete the event and semaphore.
        vEventGroupDelete(request_msg.request_complete_event);
        GSTATS_INC_FREE("request_complete_event");
        vSemaphoreDelete(request_msg.request_complete_ack_sem);
        GSTATS_INC_FREE("request_complete_ack_sem");
    }

    if (io_error) {
        ESP_LOGE(TAG, "Error in coap_io_process");
        return GOLIOTH_ERR_IO;
    }

    if (time_spent_waiting_ms >= timeout_ms) {
        ESP_LOGE(TAG, "Timeout: never got a response from the server");

        // Call user's callback with GOLIOTH_ERR_TIMEOUT
        // TODO - simplify, put callback directly in request which removes if/else branches
        golioth_response_t response = {};
        response.status = GOLIOTH_ERR_TIMEOUT;
        if (request_msg.type == GOLIOTH_COAP_REQUEST_GET && request_msg.get.callback) {
            request_msg.get.callback(
                    client, &response, request_msg.path, NULL, 0, request_msg.get.arg);
        } else if (
                request_msg.type == GOLIOTH_COAP_REQUEST_GET_BLOCK
                && request_msg.get_block.callback) {
            request_msg.get_block.callback(
                    client, &response, request_msg.path, NULL, 0, request_msg.get_block.arg);
        } else if (request_msg.type == GOLIOTH_COAP_REQUEST_POST && request_msg.post.callback) {
            request_msg.post.callback(client, &response, request_msg.path, request_msg.post.arg);
        } else if (request_msg.type == GOLIOTH_COAP_REQUEST_DELETE && request_msg.delete.callback) {
            request_msg.delete.callback(
                    client, &response, request_msg.path, request_msg.delete.arg);
        }

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
    if (c->is_running && golioth_client_num_items_in_request_queue(c) == 0 && !c->pending_req) {
        ESP_LOGD(TAG, "keepalive");
        golioth_coap_client_empty(c, false, GOLIOTH_WAIT_FOREVER);
    }
}

bool golioth_client_is_running(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return false;
    }
    return c->is_running;
}

// Note: libcoap is not thread safe, so all rx/tx I/O for the session must be
// done in this task.
static void golioth_coap_client_task(void* arg) {
    golioth_coap_client_t* client = (golioth_coap_client_t*)arg;
    assert(client);

    while (1) {
        coap_context_t* coap_context = NULL;
        coap_session_t* coap_session = NULL;

        client->end_session = false;
        client->session_connected = false;

        client->is_running = false;
        ESP_LOGD(TAG, "Waiting for the \"run\" signal");
        xSemaphoreTake(client->run_sem, portMAX_DELAY);
        xSemaphoreGive(client->run_sem);
        ESP_LOGD(TAG, "Received \"run\" signal");
        client->is_running = true;

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
        if (golioth_client_num_items_in_request_queue(client) == 0) {
            golioth_coap_client_empty(client, false, GOLIOTH_WAIT_FOREVER);
        }

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

        if (client->event_callback && client->session_connected) {
            client->event_callback(
                    client, GOLIOTH_CLIENT_EVENT_DISCONNECTED, client->event_callback_arg);
        }
        client->session_connected = false;

        if (coap_session) {
            coap_session_release(coap_session);
            GSTATS_INC_FREE("session");
        }
        if (coap_context) {
            coap_free_context(coap_context);
            GSTATS_INC_FREE("context");
        }
        coap_cleanup();

        // Small delay before starting a new session
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
    GSTATS_INC_FREE("coap_task_handle");
}

golioth_client_t golioth_client_create(const golioth_client_config_t* config) {
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
    GSTATS_INC_ALLOC("client");

    new_client->config = *config;

    new_client->run_sem = xSemaphoreCreateBinary();
    if (!new_client->run_sem) {
        ESP_LOGE(TAG, "Failed to create run semaphore");
        goto error;
    }
    GSTATS_INC_ALLOC("run_sem");
    xSemaphoreGive(new_client->run_sem);

    new_client->request_queue = xQueueCreate(
            CONFIG_GOLIOTH_COAP_REQUEST_QUEUE_MAX_ITEMS, sizeof(golioth_coap_request_msg_t));
    if (!new_client->request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        goto error;
    }
    GSTATS_INC_ALLOC("request_queue");

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
    GSTATS_INC_ALLOC("coap_task_handle");

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
    GSTATS_INC_ALLOC("keepalive_timer");

    if (CONFIG_GOLIOTH_COAP_KEEPALIVE_INTERVAL_S > 0) {
        if (!xTimerStart(new_client->keepalive_timer, 0)) {
            ESP_LOGE(TAG, "Failed to start keepalive timer");
            goto error;
        }
    }

    new_client->is_running = true;

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
        GSTATS_INC_FREE("keepalive_timer");
    }
    if (c->coap_task_handle) {
        vTaskDelete(c->coap_task_handle);
        GSTATS_INC_FREE("coap_task_handle");
    }
    // TODO: purge queue, free dyn mem for requests that have it
    if (c->request_queue) {
        vQueueDelete(c->request_queue);
        GSTATS_INC_FREE("request_queue");
    }
    if (c->run_sem) {
        vSemaphoreDelete(c->run_sem);
        GSTATS_INC_FREE("run_sem");
    }
    free(c);
    GSTATS_INC_FREE("client");
}

bool golioth_client_is_connected(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return false;
    }
    return c->session_connected;
}

golioth_status_t golioth_coap_client_empty(
        golioth_client_t client,
        bool is_synchronous,
        int32_t timeout_s) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    if (!c->is_running) {
        ESP_LOGW(TAG, "Client not running, dropping request");
        return GOLIOTH_ERR_INVALID_STATE;
    }

    uint64_t ageout_ms = GOLIOTH_WAIT_FOREVER;
    if (timeout_s != GOLIOTH_WAIT_FOREVER) {
        ageout_ms = golioth_time_millis() + (1000 * timeout_s);
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_EMPTY,
            .ageout_ms = ageout_ms,
    };

    if (is_synchronous) {
        // Created here, deleted by coap task (or here if fail to enqueue
        request_msg.request_complete_event = xEventGroupCreate();
        GSTATS_INC_ALLOC("request_complete_event");
        request_msg.request_complete_ack_sem = xSemaphoreCreateBinary();
        GSTATS_INC_ALLOC("request_complete_ack_sem");
    }

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        if (is_synchronous) {
            vEventGroupDelete(request_msg.request_complete_event);
            GSTATS_INC_FREE("request_complete_event");
            vSemaphoreDelete(request_msg.request_complete_ack_sem);
            GSTATS_INC_FREE("request_complete_ack_sem");
        }
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    if (is_synchronous) {
        uint64_t tmo_ticks =
                (timeout_s == GOLIOTH_WAIT_FOREVER ? portMAX_DELAY
                                                   : (timeout_s * 1000) / portTICK_PERIOD_MS);
        EventBits_t bits = xEventGroupWaitBits(
                request_msg.request_complete_event,
                RESPONSE_RECEIVED_EVENT_BIT | RESPONSE_TIMEOUT_EVENT_BIT,
                pdTRUE,   // clear bits after waiting
                pdFALSE,  // either bit can trigger
                tmo_ticks);

        // Notify CoAP task that we received the event
        xSemaphoreGive(request_msg.request_complete_ack_sem);

        if ((bits == 0) || (bits & RESPONSE_TIMEOUT_EVENT_BIT)) {
            return GOLIOTH_ERR_TIMEOUT;
        }
    }
    return GOLIOTH_OK;
}

golioth_status_t golioth_coap_client_set(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        const uint8_t* payload,
        size_t payload_size,
        golioth_set_cb_fn callback,
        void* callback_arg,
        bool is_synchronous,
        int32_t timeout_s) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    uint8_t* request_payload = NULL;

    if (!c->is_running) {
        ESP_LOGW(TAG, "Client not running, dropping request for path %s", path);
        return GOLIOTH_ERR_INVALID_STATE;
    }

    if (payload_size > 0) {
        // We will allocate memory and copy the payload
        // to avoid payload lifetime and thread-safety issues.
        //
        // This memory will be free'd by the CoAP task after handling the request,
        // or in this function if we fail to enqueue the request.
        request_payload = (uint8_t*)calloc(1, payload_size);
        if (!request_payload) {
            ESP_LOGE(TAG, "Payload alloc failure");
            return GOLIOTH_ERR_MEM_ALLOC;
        }
        GSTATS_INC_ALLOC("request_payload");
        memcpy(request_payload, payload, payload_size);
    }

    uint64_t ageout_ms = GOLIOTH_WAIT_FOREVER;
    if (timeout_s != GOLIOTH_WAIT_FOREVER) {
        ageout_ms = golioth_time_millis() + (1000 * timeout_s);
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_POST,
            .path_prefix = path_prefix,
            .post =
                    {
                            .content_type = content_type,
                            .payload = request_payload,
                            .payload_size = payload_size,
                            .callback = callback,
                            .arg = callback_arg,
                    },
            .ageout_ms = ageout_ms,
    };
    strncpy(request_msg.path, path, sizeof(request_msg.path) - 1);

    if (is_synchronous) {
        // Created here, deleted by coap task (or here if fail to enqueue
        request_msg.request_complete_event = xEventGroupCreate();
        GSTATS_INC_ALLOC("request_complete_event");
        request_msg.request_complete_ack_sem = xSemaphoreCreateBinary();
        GSTATS_INC_ALLOC("request_complete_ack_sem");
    }

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        if (payload_size > 0) {
            free(request_payload);
            GSTATS_INC_FREE("request_payload");
        }
        if (is_synchronous) {
            vEventGroupDelete(request_msg.request_complete_event);
            GSTATS_INC_FREE("request_complete_event");
            vSemaphoreDelete(request_msg.request_complete_ack_sem);
            GSTATS_INC_FREE("request_complete_ack_sem");
        }
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    if (is_synchronous) {
        uint64_t tmo_ticks =
                (timeout_s == GOLIOTH_WAIT_FOREVER ? portMAX_DELAY
                                                   : (timeout_s * 1000) / portTICK_PERIOD_MS);
        EventBits_t bits = xEventGroupWaitBits(
                request_msg.request_complete_event,
                RESPONSE_RECEIVED_EVENT_BIT | RESPONSE_TIMEOUT_EVENT_BIT,
                pdTRUE,   // clear bits after waiting
                pdFALSE,  // either bit can trigger
                tmo_ticks);

        // Notify CoAP task that we received the event
        xSemaphoreGive(request_msg.request_complete_ack_sem);

        if ((bits == 0) || (bits & RESPONSE_TIMEOUT_EVENT_BIT)) {
            return GOLIOTH_ERR_TIMEOUT;
        }
    }
    return GOLIOTH_OK;
}

golioth_status_t golioth_coap_client_delete(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        golioth_set_cb_fn callback,
        void* callback_arg,
        bool is_synchronous,
        int32_t timeout_s) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    if (!c->is_running) {
        ESP_LOGW(TAG, "Client not running, dropping request for path %s", path);
        return GOLIOTH_ERR_INVALID_STATE;
    }

    uint64_t ageout_ms = GOLIOTH_WAIT_FOREVER;
    if (timeout_s != GOLIOTH_WAIT_FOREVER) {
        ageout_ms = golioth_time_millis() + (1000 * timeout_s);
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_DELETE,
            .path_prefix = path_prefix,
            .delete =
                    {
                            .callback = callback,
                            .arg = callback_arg,
                    },
            .ageout_ms = ageout_ms,
    };
    strncpy(request_msg.path, path, sizeof(request_msg.path) - 1);

    if (is_synchronous) {
        // Created here, deleted by coap task (or here if fail to enqueue
        request_msg.request_complete_event = xEventGroupCreate();
        GSTATS_INC_ALLOC("request_complete_event");
        request_msg.request_complete_ack_sem = xSemaphoreCreateBinary();
        GSTATS_INC_ALLOC("request_complete_ack_sem");
    }

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGW(TAG, "Failed to enqueue request, queue full");
        if (is_synchronous) {
            vEventGroupDelete(request_msg.request_complete_event);
            GSTATS_INC_FREE("request_complete_event");
            vSemaphoreDelete(request_msg.request_complete_ack_sem);
            GSTATS_INC_FREE("request_complete_ack_sem");
        }
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    if (is_synchronous) {
        uint64_t tmo_ticks =
                (timeout_s == GOLIOTH_WAIT_FOREVER ? portMAX_DELAY
                                                   : (timeout_s * 1000) / portTICK_PERIOD_MS);
        EventBits_t bits = xEventGroupWaitBits(
                request_msg.request_complete_event,
                RESPONSE_RECEIVED_EVENT_BIT | RESPONSE_TIMEOUT_EVENT_BIT,
                pdTRUE,   // clear bits after waiting
                pdFALSE,  // either bit can trigger
                tmo_ticks);

        // Notify CoAP task that we received the event
        xSemaphoreGive(request_msg.request_complete_ack_sem);

        if ((bits == 0) || (bits & RESPONSE_TIMEOUT_EVENT_BIT)) {
            return GOLIOTH_ERR_TIMEOUT;
        }
    }
    return GOLIOTH_OK;
}

static golioth_status_t golioth_coap_client_get_internal(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        golioth_coap_request_type_t type,
        void* request_params,
        bool is_synchronous,
        int32_t timeout_s) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return GOLIOTH_ERR_NULL;
    }

    if (!c->is_running) {
        ESP_LOGW(TAG, "Client not running, dropping get request");
        return GOLIOTH_ERR_INVALID_STATE;
    }

    uint64_t ageout_ms = GOLIOTH_WAIT_FOREVER;
    if (timeout_s != GOLIOTH_WAIT_FOREVER) {
        ageout_ms = golioth_time_millis() + (1000 * timeout_s);
    }

    golioth_coap_request_msg_t request_msg = {};
    request_msg.type = type;
    request_msg.path_prefix = path_prefix;
    strncpy(request_msg.path, path, sizeof(request_msg.path) - 1);
    if (is_synchronous) {
        // Created here, deleted by coap task (or here if fail to enqueue
        request_msg.request_complete_event = xEventGroupCreate();
        GSTATS_INC_ALLOC("request_complete_event");
        request_msg.request_complete_ack_sem = xSemaphoreCreateBinary();
        GSTATS_INC_ALLOC("request_complete_ack_sem");
    }
    request_msg.ageout_ms = ageout_ms;
    if (type == GOLIOTH_COAP_REQUEST_GET_BLOCK) {
        request_msg.get_block = *(golioth_coap_get_block_params_t*)request_params;
    } else {
        assert(type == GOLIOTH_COAP_REQUEST_GET);
        request_msg.get = *(golioth_coap_get_params_t*)request_params;
    }

    BaseType_t sent = xQueueSend(c->request_queue, &request_msg, 0);
    if (!sent) {
        ESP_LOGE(TAG, "Failed to enqueue request, queue full");
        if (is_synchronous) {
            vEventGroupDelete(request_msg.request_complete_event);
            GSTATS_INC_FREE("request_complete_event");
            vSemaphoreDelete(request_msg.request_complete_ack_sem);
            GSTATS_INC_FREE("request_complete_ack_sem");
        }
        return GOLIOTH_ERR_QUEUE_FULL;
    }

    if (is_synchronous) {
        uint64_t tmo_ticks =
                (timeout_s == GOLIOTH_WAIT_FOREVER ? portMAX_DELAY
                                                   : (timeout_s * 1000) / portTICK_PERIOD_MS);
        EventBits_t bits = xEventGroupWaitBits(
                request_msg.request_complete_event,
                RESPONSE_RECEIVED_EVENT_BIT | RESPONSE_TIMEOUT_EVENT_BIT,
                pdTRUE,   // clear bits after waiting
                pdFALSE,  // either bit can trigger
                tmo_ticks);

        // Notify CoAP task that we received the event
        xSemaphoreGive(request_msg.request_complete_ack_sem);

        if ((bits == 0) || (bits & RESPONSE_TIMEOUT_EVENT_BIT)) {
            return GOLIOTH_ERR_TIMEOUT;
        }
    }
    return GOLIOTH_OK;
}

golioth_status_t golioth_coap_client_get(
        golioth_client_t client,
        const char* path_prefix,
        const char* path,
        uint32_t content_type,
        golioth_get_cb_fn callback,
        void* arg,
        bool is_synchronous,
        int32_t timeout_s) {
    golioth_coap_get_params_t params = {
            .content_type = content_type,
            .callback = callback,
            .arg = arg,
    };
    return golioth_coap_client_get_internal(
            client,
            path_prefix,
            path,
            GOLIOTH_COAP_REQUEST_GET,
            &params,
            is_synchronous,
            timeout_s);
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
        bool is_synchronous,
        int32_t timeout_s) {
    golioth_coap_get_block_params_t params = {
            .content_type = content_type,
            .block_index = block_index,
            .block_size = block_size,
            .callback = callback,
            .arg = arg,
    };
    return golioth_coap_client_get_internal(
            client,
            path_prefix,
            path,
            GOLIOTH_COAP_REQUEST_GET_BLOCK,
            &params,
            is_synchronous,
            timeout_s);
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

    if (!c->is_running) {
        ESP_LOGW(TAG, "Client not running, dropping request for path %s", path);
        return GOLIOTH_ERR_INVALID_STATE;
    }

    golioth_coap_request_msg_t request_msg = {
            .type = GOLIOTH_COAP_REQUEST_OBSERVE,
            .path_prefix = path_prefix,
            .ageout_ms = GOLIOTH_WAIT_FOREVER,
            .observe =
                    {
                            .content_type = content_type,
                            .callback = callback,
                            .arg = arg,
                    },
    };
    strncpy(request_msg.path, path, sizeof(request_msg.path) - 1);

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

uint32_t golioth_client_task_stack_min_remaining(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return 0;
    }
    return uxTaskGetStackHighWaterMark(c->coap_task_handle);
}

void golioth_client_set_packet_loss_percent(uint8_t percent) {
    if (percent > 100) {
        ESP_LOGE(TAG, "Invalid percent %u, must be 0 to 100", percent);
        return;
    }
    static char buf[16] = {};
    snprintf(buf, sizeof(buf), "%u%%", percent);
    ESP_LOGI(TAG, "Setting packet loss to %s", buf);
    coap_debug_set_packet_loss(buf);
}

uint32_t golioth_client_num_items_in_request_queue(golioth_client_t client) {
    golioth_coap_client_t* c = (golioth_coap_client_t*)client;
    if (!c) {
        return 0;
    }
    return uxQueueMessagesWaiting(c->request_queue);
}

bool golioth_client_has_allocation_leaks(void) {
    return golioth_statistics_has_allocation_leaks();
}
