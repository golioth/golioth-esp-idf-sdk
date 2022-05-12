idf_component_register(
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "priv_include"
    PRIV_REQUIRES
        "coap"
    SRCS
        "golioth_status.c"
        "golioth_coap_client.c"
        "golioth_log.c"
        "golioth_lightdb.c"
)
target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG")
