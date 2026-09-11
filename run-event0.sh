#!/bin/sh
set -eu
FSB_CORE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FSB_HOST_BUILD="${FSB_HOST_BUILD_DIR:-$FSB_CORE_DIR/.build-host}"
cmake -S "$FSB_CORE_DIR" -B "$FSB_HOST_BUILD" -DCMAKE_BUILD_TYPE=Release -DFSB_CORE_BUILD_SDL_HOST=ON
cmake --build "$FSB_HOST_BUILD" --target fsb_event0_sdl --parallel
# Snapshot-vault change: assets/ is not stored in this repo, so the path is overridable.
exec "$FSB_HOST_BUILD/fsb_event0_sdl" "${FSB_ASSETS_DIR:-$FSB_CORE_DIR/assets}" "$@"
