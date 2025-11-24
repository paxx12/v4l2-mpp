#!/bin/bash
set -e

DIR=$(realpath "$(dirname "$0")")
cd "$DIR"

set -xeo pipefail

cd libdatachannel

cmake -S . -B build/ \
    -DCMAKE_BUILD_TYPE=Release

make -C build/ -j5 datachannel-static
