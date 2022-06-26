#!/bin/bash

THIS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$THIS_DIR/../.."

find $REPO_ROOT/components -type f -name "*.c" -o -name "*.h" \
    | grep -v -E ".*third_party.*" \
    | xargs clang-format -i -style=file --verbose

find $REPO_ROOT/examples/*/main -type f -name "*.c" -o -name "*.h" \
    | grep -v -E ".*build.*" \
    | xargs clang-format -i -style=file --verbose
