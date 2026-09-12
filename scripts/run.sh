#!/bin/sh
set -eu
sentinel_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sentinel_runtime="$sentinel_repo/.local-deps"
if [ -d "$sentinel_runtime/bin" ]; then
    export PATH="$sentinel_runtime/bin:$PATH"
fi
if [ -d "$sentinel_runtime/lib" ]; then
    export LD_LIBRARY_PATH="$sentinel_runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
sentinel_binary="$sentinel_repo/build/sentinel-studio"
if [ ! -x "$sentinel_binary" ]; then
    echo "Build Sentinel Studio first; see README.md." >&2
    exit 1
fi
exec "$sentinel_binary" "$@"
