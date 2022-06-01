idf_component_register(
    INCLUDE_DIRS
        "."
    REQUIRES
        "console"
        "spi_flash"
        "nvs_flash"
    SRCS
        "cmd_nvs.c"
        "cmd_system.c"
        "cmd_wifi.c"
        "golioth_shell.c"
)

