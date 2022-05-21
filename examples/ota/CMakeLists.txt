cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS
    # Use esp-idf example components for connecting to WiFi
    $ENV{IDF_PATH}/examples/common_components/protocol_examples_common
    # Golioth components
    ../../components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ota)
