# Golioth esp-idf SDK

A software development kit for connecting Espressif devices to the
[Golioth](golioth.io) IoT cloud.

This repo contains a runtime library and set of examples intended to build
and run in the latest release of esp-idf
(currently [4.4.1](https://github.com/espressif/esp-idf/releases/tag/v4.4.1)).

## Cloning this repo

This repo uses git submodules, so you will need to clone with the `--recursive` option:

```sh
git clone --recursive https://github.com/golioth/golioth-esp-idf-sdk.git
```

Or, if you've already cloned, you can update and initialize submodules with this command:

```sh
cd golioth-esp-idf-sdk
git submodule update --init --recursive
```

## Install esp-idf

Install the latest release of esp-idf using the
[installation directions from Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#installation)

## Trying the examples

The `examples` directory contains example apps which you can build and run.

### Using VSCode esp-idf extension

If you are using the VScode esp-idf extension, you should be able to load these examples
directly into your workspace and build/flash/monitor.

### Command-line

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
