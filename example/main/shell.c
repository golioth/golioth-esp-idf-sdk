/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "shell.h"
#include "wifi.h"

#define TAG "shell"
#define PROMPT_STR CONFIG_IDF_TARGET
#define COUNT_OF(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

static struct {
    struct arg_str* ssid;
    struct arg_str* password;
    struct arg_end* end;
} _wifi_set_args;

static struct {
    struct arg_str* psk_id;
    struct arg_str* psk;
    struct arg_end* end;
} _golioth_set_args;

static int heap(int argc, char** argv);
static int version(int argc, char** argv);
static int restart(int argc, char** argv);
static int tasks(int argc, char** argv);
static int wifi_set(int argc, char** argv);
static int wifi_get(int argc, char** argv);
static int wifi_erase(int argc, char** argv);
static int golioth_set(int argc, char** argv);
static int golioth_get(int argc, char** argv);
static int golioth_erase(int argc, char** argv);

static const esp_console_cmd_t _cmds[] = {
        {
                .command = "heap",
                .help = "Get the current size of free heap memory, and min ever heap size",
                .hint = NULL,
                .func = heap,
        },
        {
                .command = "version",
                .help = "Get version of chip and SDK",
                .hint = NULL,
                .func = version,
        },
        {
                .command = "restart",
                .help = "Software reset of the chip",
                .hint = NULL,
                .func = restart,
        },
        {
                .command = "tasks",
                .help = "Get information about running tasks and stack high watermark (HWM)",
                .hint = NULL,
                .func = tasks,
        },
        {
                .command = "wifi_set",
                .help = "Set WiFi SSID/password",
                .hint = NULL,
                .func = wifi_set,
                .argtable = &_wifi_set_args,
        },
        {
                .command = "wifi_get",
                .help = "Get WiFi SSID/password",
                .hint = NULL,
                .func = wifi_get,
        },
        {
                .command = "wifi_erase",
                .help = "Erase WiFi SSID/password",
                .hint = NULL,
                .func = wifi_erase,
        },
        {
                .command = "golioth_set",
                .help = "Set Golioth PSK-ID/PSK",
                .hint = NULL,
                .func = golioth_set,
                .argtable = &_golioth_set_args,
        },
        {
                .command = "golioth_get",
                .help = "Get Golioth PSK-ID/PSK",
                .hint = NULL,
                .func = golioth_get,
        },
        {
                .command = "golioth_erase",
                .help = "Erase Golioth PSK-ID/PSK",
                .hint = NULL,
                .func = golioth_erase,
        },
};

static int heap(int argc, char** argv) {
    printf("Free: %d, Free low watermark: %d\n",
           esp_get_free_heap_size(),
           heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
    return 0;
}

static int version(int argc, char** argv) {
    esp_chip_info_t info;
    esp_chip_info(&info);
    printf("IDF Version:%s\r\n", esp_get_idf_version());
    printf("Chip info:\r\n");
    printf("\tmodel:%s\r\n", info.model == CHIP_ESP32 ? "ESP32" : "Unknown");
    printf("\tcores:%d\r\n", info.cores);
    printf("\tfeature:%s%s%s%s%d%s\r\n",
           info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
           info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
           info.features & CHIP_FEATURE_BT ? "/BT" : "",
           info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
           spi_flash_get_chip_size() / (1024 * 1024),
           " MB");
    printf("\trevision number:%d\r\n", info.revision);
    return 0;
}

static int restart(int argc, char** argv) {
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

static int tasks(int argc, char** argv) {
    const size_t bytes_per_task = 40;
    char* task_list_buffer = malloc(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (task_list_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate buffer for vTaskList output");
        return 1;
    }
    fputs("Task Name\tStatus\tPrio\tHWM\tTask#", stdout);
    fputs("\n", stdout);
    vTaskList(task_list_buffer);
    fputs(task_list_buffer, stdout);
    free(task_list_buffer);
    return 0;
}

static int wifi_set(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**)&_wifi_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, _wifi_set_args.end, argv[0]);
        return 1;
    }

    const char* ssid = _wifi_set_args.ssid->sval[0];
    const char* password = _wifi_set_args.password->sval[0];

    nvs_write_str(NVS_WIFI_SSID_KEY, ssid);
    nvs_write_str(NVS_WIFI_PASS_KEY, password);
    ESP_LOGI(TAG, "Saved SSID and password to NVS");

    return 0;
}

static int wifi_get(int argc, char** argv) {
    printf("SSID: %s\n", nvs_read_wifi_ssid());

    const char* password = nvs_read_wifi_password();
    size_t password_len = strlen(password);
    printf("Password: %c", password[0]);
    for (int i = 1; i < password_len; i++) {
        printf("*");
    }
    printf("\n");
    return 0;
}

static int wifi_erase(int argc, char** argv) {
    nvs_erase_str(NVS_WIFI_SSID_KEY);
    nvs_erase_str(NVS_WIFI_PASS_KEY);
    ESP_LOGI(TAG, "Erase SSID and password from NVS");
    return 0;
}

static int golioth_set(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**)&_golioth_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, _golioth_set_args.end, argv[0]);
        return 1;
    }

    const char* psk_id = _golioth_set_args.psk_id->sval[0];
    const char* psk = _golioth_set_args.psk->sval[0];

    nvs_write_str(NVS_GOLIOTH_PSK_ID_KEY, psk_id);
    nvs_write_str(NVS_GOLIOTH_PSK_KEY, psk);
    ESP_LOGI(TAG, "Saved PSK ID and PSK to NVS");

    return 0;
}

static int golioth_erase(int argc, char** argv) {
    nvs_erase_str(NVS_GOLIOTH_PSK_ID_KEY);
    nvs_erase_str(NVS_GOLIOTH_PSK_KEY);
    ESP_LOGI(TAG, "Erase PSK ID and PSK from NVS");
    return 0;
}

static int golioth_get(int argc, char** argv) {
    printf("PSK-ID: %s\n", nvs_read_golioth_psk_id());

    const char* psk = nvs_read_golioth_psk();
    size_t psk_len = strlen(psk);
    printf("PSK: %c", psk[0]);
    for (int i = 1; i < psk_len; i++) {
        printf("*");
    }
    printf("\n");
    return 0;
}

static void initialize_argtables() {
    _wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    _wifi_set_args.password = arg_str1(NULL, NULL, "<pass>", "Password/PSK of AP");
    _wifi_set_args.end = arg_end(2);

    _golioth_set_args.psk_id =
            arg_str1(NULL, NULL, "<psk_id>", "Pre-shared key ID (contains @ character)");
    _golioth_set_args.psk = arg_str1(NULL, NULL, "<psk>", "Pre-shared key");
    _golioth_set_args.end = arg_end(2);
}

static void initialize_console(void) {
    fflush(stdout);
    fsync(fileno(stdout));
    setvbuf(stdin, NULL, _IONBF, 0);
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
        .source_clk = UART_SCLK_REF_TICK,
#else
        .source_clk = UART_SCLK_XTAL,
#endif
    };
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_console_config_t console_config = {
            .max_cmdline_args = 8, .max_cmdline_length = 256, .hint_color = atoi(LOG_COLOR_CYAN)};
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*)&esp_console_get_hint);
    linenoiseHistorySetMaxLen(100);
    linenoiseSetMaxLineLen(console_config.max_cmdline_length);
    linenoiseAllowEmpty(false);
}

static void shell_task(void* arg) {
    initialize_console();
    esp_console_register_help_command();

    initialize_argtables();

    for (int i = 0; i < COUNT_OF(_cmds); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&_cmds[i]));
    }

    const char* prompt = LOG_COLOR_I PROMPT_STR "> " LOG_RESET_COLOR;
    printf("\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n"
           "Press Enter or Ctrl+C will terminate the console environment.\n");

    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
        prompt = PROMPT_STR "> ";
    }

    while (true) {
        char* line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }

        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
        }

        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }
}

void shell_init(void) {
    static bool initialized = false;
    if (!initialized) {
        bool task_created = xTaskCreate(
                shell_task,
                "shell",
                7168,
                NULL,  // task arg
                2,     // pri
                NULL);
        if (!task_created) {
            ESP_LOGE(TAG, "Failed to create shell task");
        } else {
            initialized = true;
        }
    }
}
