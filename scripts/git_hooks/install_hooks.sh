#!/bin/bash -e

# change to this script's directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

REPO_ROOT=../..

# get the git base directory
mkdir -p "$REPO_ROOT/.git/hooks"

# get all hooks (but not this file)
for hook in *; do
    if [[ "$hook" == "install_hooks.sh" ]]; then continue; fi
    cp "$hook" "$REPO_ROOT/.git/hooks"
done
