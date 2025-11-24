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
./genMakefiles linux-no-std-lib
rm -rf usr-local
make -j$(nproc) install PREFIX="$(pwd)/usr-local"
