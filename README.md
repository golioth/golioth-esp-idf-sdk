# Golioth esp-idf SDK

A software development kit for connecting Espressif devices to the
[Golioth](https://golioth.io) IoT cloud.

This repo contains a runtime library and set of examples intended to build
and run in the latest release of esp-idf
(currently [4.4.1](https://github.com/espressif/esp-idf/releases/tag/v4.4.1)).

## Cloning this repo

This repo uses git submodules, so you will need to clone with the `--recursive` option:

```sh
git clone --recursive https://github.com/golioth/golioth-esp-idf-sdk.git
```

Or, if you've already cloned but forgot the `--recursive`, you can update and
initialize submodules with this command:

```sh
cd golioth-esp-idf-sdk
git submodule update --init --recursive
```

## Install esp-idf

Install version 4.4.1 of esp-idf using the
[installation directions from Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#installation).
This is the version of esp-idf this SDK is tested against.

For Linux users, you can install esp-idf with these commands:

```sh
sudo apt-get install git wget flex bison gperf python3 python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v4.4.1
git submodule update --init --recursive
./install.sh all
```

## Trying the Golioth SDK examples

The `examples` directory contains example apps which you can build and run.

The `golioth_basics` example is recommended as a starting point, to learn how to
connect to Golioth and use services like LightDB state, LightDB stream, Logging, and OTA.

### Using VSCode esp-idf extension

If you are using the VScode esp-idf extension, you should be able to load these examples
directly into your workspace and build/flash/monitor.

### Command-line

First, setup the environment. This step assumes you've installed esp-idf to `~/esp/esp-idf`

```sh
source ~/esp/esp-idf/export.sh
```

Next, `cd` to one of the examples, where you can build/flash/monitor:

```
cd examples/golioth_basics
idf.py build
idf.py flash
idf.py monitor
```

## Integrating the Golioth SDK

The recommended way to integrate this SDK into an external application is to add it as a
git submodule. For example:

```
cd your_project
git submodule add https://github.com/golioth/golioth-esp-idf-sdk.git third_party/golioth-esp-idf-sdk
git submodule update --init --recursive
```

You should not need to modify the files in the submodule at all.

The SDK is packaged as an esp-idf component, so you will need to add this line
to your project's top-level CMakeLists.txt, which will allow esp-idf to find the SDK:

```
list(APPEND EXTRA_COMPONENT_DIRS third_party/golioth-esp-idf-sdk/components)
```

Optionally, you can copy the files from `examples/common` into your project and modify
as needed. These files can be used to help with basic system initialization of
NVS, WiFi, and shell.

A typical project structure will look something like this:

```
your_project
├── CMakeLists.txt
├── main
│   ├── app_main.c
│   ├── CMakeLists.txt
├── sdkconfig.defaults
└── third_party
    └── golioth-esp-idf-sdk (submodule)
```

A complete example of using the Golioth SDK in an external project can be found here:

https://github.com/golioth/golioth-esp-idf-external-app.git
