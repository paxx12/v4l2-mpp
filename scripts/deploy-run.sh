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
make -s install DESTDIR=$PWD/tmp/v4l2
lftp -u root,snapmaker "sftp://$SSH_HOST" <<EOF
mirror -R --ignore-time tmp/v4l2/usr/local/bin/ /tmp/v4l2
mirror -R --ignore-time scripts/ /tmp/v4l2
EOF
ssh -t "$SSH_HOST" "/tmp/v4l2/$CMD" "$@"
