#!/usr/bin/env bash
set -euo pipefail
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export LIMA_HOME="$ROOT/.l"
export LIMA_CACHE_HOME="$ROOT/.work/lima-cache"
export TMPDIR="$ROOT/.work/tmp"
export XDG_CACHE_HOME="$ROOT/.work/xdg-cache"
mkdir -p "$TMPDIR" "$LIMA_CACHE_HOME" "$XDG_CACHE_HOME"
exec "$ROOT/.work/p2-tools/bin/limactl" "$@"
