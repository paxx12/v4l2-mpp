#!/bin/bash

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <host> <cmd> [additional options]"
  exit 1
fi

DIR="$(dirname "$0")"
cd "$DIR/.."

SSH_HOST="$1"
CMD="$2"
shift 2

set -xeo pipefail
make install DESTDIR=$PWD/tmp/v4l2
scp -r tmp/v4l2/. scripts/run-*.sh "$SSH_HOST":/tmp/v4l2
ssh -t "$SSH_HOST" "/tmp/v4l2/$CMD" "$@"
