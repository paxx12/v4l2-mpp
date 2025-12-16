#!/bin/bash

FILENAME=live.2025.11.06.tar.gz
URL=http://www.live555.com/liveMedia/public/live.2025.11.06.tar.gz
SHA256=7614fa0a293e61b24bfd715a30a1c020fb4fe5490ebb02e71b0dadb5efc1d17c

DIR=$(realpath "$(dirname "$0")")
cd "$DIR"

set -xeo pipefail

if [[ ! -f "$FILENAME" ]]; then
  wget -O "$FILENAME" "$URL"
fi

echo "$SHA256  $FILENAME" | sha256sum --check --status

if [[ ! -d live ]]; then
  rm -rf live
  tar xzf "$FILENAME"
fi

cd live

CONFIG_FILE=linux.no-std-lib

if [[ -n "$CROSS_COMPILE" ]]; then
  cp config.armlinux config.armlinux-no-std-lib
  echo "CPLUSPLUS_FLAGS += -DNO_STD_LIB=1" >> config.armlinux-no-std-lib
  CONFIG_FILE=armlinux-no-std-lib
fi

./genMakefiles "$CONFIG_FILE"
rm -rf usr-local
make -j$(nproc) install PREFIX="$(pwd)/usr-local"
