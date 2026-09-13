#!/bin/sh
set -eu
sentinel_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sentinel_sdk="$sentinel_repo/.local-deps/sdk/usr"
if [ -x "$sentinel_sdk/bin/cmake" ]; then
    export LD_LIBRARY_PATH="$sentinel_sdk/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    exec "$sentinel_sdk/bin/cmake" "$@"
fi
exec cmake "$@"
