#!/bin/sh
set -eu
FSB_LAUNCH_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$FSB_LAUNCH_DIR/run-event0.sh" --campaign --intro --reward-boost "$@"
