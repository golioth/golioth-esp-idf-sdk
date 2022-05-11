cmake_minimum_required(VERSION 3.5)

# To simplify connecting to WiFi, we use extra components from esp-idf examples.
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(coap_minimal)

