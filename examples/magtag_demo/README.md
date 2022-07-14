## Environment Setup

```sh
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s2
```

## Configure

You will need to set the following config items in order
to connect to WiFi and Golioth:

```sh
CONFIG_GOLIOTH_EXAMPLE_WIFI_SSID
CONFIG_GOLIOTH_EXAMPLE_WIFI_PSK
CONFIG_GOLIOTH_EXAMPLE_COAP_PSK_ID
CONFIG_GOLIOTH_EXAMPLE_COAP_PSK
```

You can set these in `sdkconfig.defaults` or use menuconfig:

```sh
cd examples/magtag_demo
idf.py menuconfig
```

## Build

```sh
cd examples/magtag_demo
idf.py build
```

## Known issues

Newer versions of the ESP-IDF may fail at build time with the following message:

```sh
#warning "The legacy RMT driver is deprecated, please use driver/rmt_tx.h and/or driver/rmt_rx.h" [-Werror=cpp]
```

To successfully build the project, suppress the warning by adding this line to
`skdconfig.defaults` or using menuconfig:

```sh
CONFIG_RMT_SUPPRESS_DEPRECATE_WARN=y
```

After making this change, remove the `sdkconfig` file and rebuild the project.

## Flash

On the magtag board, put the ESP32 into bootloader mode so it can
be flashed:

1. press and hold the `Boot0` button
2. press the `Reset` button (while `Boot0` is held)

Now that the board is in bootloader mode, you can flash:

```sh
idf.py -p /dev/ttyACM0 flash
```

Finally, press the `Reset` button once more to reboot
the board into the new firmware.

## Serial monitoring

The magtag board does not expose serial UART Rx/Tx over USB,

However, there are test points on the board labelled `RX` and `TX`.
So if you have a USB-to-UART adapter/cable, you can
connect the adapter Rx wire to the `TX` test point to see
serial output, using a serial terminal program:

```sh
minicom -D /dev/ttyUSB0 -b 115200 --color=on
```
