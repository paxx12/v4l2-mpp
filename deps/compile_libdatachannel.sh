#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
# SPDX-FileCopyrightText: Copyright (c) 2025 @paxx12

set -e

DIR=$(realpath "$(dirname "$0")")
cd "$DIR"

set -xeo pipefail

cd libdatachannel

cmake -S . -B build/ \
    -DCMAKE_BUILD_TYPE=Release

make -C build/ -j5 datachannel-static
