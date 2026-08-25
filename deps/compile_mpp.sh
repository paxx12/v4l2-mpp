#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
# SPDX-FileCopyrightText: Copyright (c) 2025 @paxx12

DIR=$(realpath "$(dirname "$0")")
cd "$DIR"

set -xeo pipefail

cd mpp
cmake . \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TEST=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/usr-local"
make -j5 install

# Cleanup unneeded files to avoid linking to .so files
rm -rf usr-local/lib/librockchip*.so*
