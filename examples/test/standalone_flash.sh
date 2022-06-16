esptool.py \
    --port $ESPPORT \
    --baud 460800 \
    --before default_reset --after hard_reset \
    write_flash \
    --flash_mode dio --flash_size detect --flash_freq 40m \
    ${BOOTLOADER_OFFSET} "${BOOTLOADER_FILENAME}" \
    ${PARTITION_TABLE_OFFSET} "${PARTITION_TABLE_FILENAME}" \
    ${OTA_DATA_OFFSET} "${OTA_DATA_FILENAME}" \
    ${OTA_0_OFFSET} "${OTA_0_FILENAME}"
