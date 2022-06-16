#!/bin/bash
tar czf \
    build.tar.gz \
    build/flasher_args.json \
    build/bootloader/bootloader.bin \
    build/test.bin \
    build/partition_table/partition-table.bin \
    build/ota_data_initial.bin
