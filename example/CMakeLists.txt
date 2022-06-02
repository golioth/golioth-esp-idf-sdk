cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS
    # Golioth components and dependencies
    ../components/third_party
    ../components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(golioth_example)
