# Golioth esp-idf SDK

A software development kit for connecting Espressif devices to the
[Golioth](golioth.io) IoT cloud.

This repo contains a runtime library and set of examples intended to build
and run in the latest release of esp-idf
(currently [4.4.1](https://github.com/espressif/esp-idf/releases/tag/v4.4.1)).

## Install esp-idf

Install the latest release of esp-idf using the
[installation directions from Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#installation)

## Trying the examples

The `examples` directory contains example apps which you can build and run.

First, setup the environment. If you've installed esp-idf using Windows
or an IDE plugin, you can usually skip this step, as the environment
will already be set up for you in those tools. For everyone else, this
step assumes you've installed esp-idf to `~/esp/esp-idf`.

```sh
source ~/esp/esp-idf/export.sh
```

By default, esp-idf assumes the target is `esp32`, but if you have a different
target, such as the `esp32s3`, set the target:

```sh
idf.py set-target esp32s3
```

Next, `cd` to the example directory:

```
cd examples/X
```

Configure (optional):

```sh
idf.py menuconfig
```

Build:

```sh
idf.py build
```

Flash (change port to match your system):

```sh
idf.py -p /dev/ttyUSB0 flash
```

Monitor (connects to ESP32 serial port):

```sh
idf.py -p /dev/ttyUSB0 monitor
```
