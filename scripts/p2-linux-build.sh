#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE=$(realpath "${1:?source directory required}")
OUTPUT=$(realpath -m "${2:?output directory required}")
[[ $(uname -s) == Linux ]] || { echo 'This entry point requires GNU/Linux'; exit 1; }
if [[ -e "$SOURCE/Common/edk2/MdePkg/Include/uefi.h" ]]; then
  echo 'Case-insensitive source filesystem: use the project VM ext4 workspace.' >&2
  exit 1
fi
mkdir -p "$OUTPUT" "$ROOT/.work/tmp"
export TMPDIR="$ROOT/.work/tmp"
export PYTHONDONTWRITEBYTECODE=1
export PYTHON_COMMAND=python3
export CROSS_COMPILE=aarch64-linux-gnu-
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=safe.directory
export GIT_CONFIG_VALUE_0='*'
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy
[[ $(aarch64-linux-gnu-gcc -dumpmachine) == aarch64-linux-gnu ]]
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git -C "$SOURCE" log -1 --format=%ct)
cd "$SOURCE"
bash build.sh --device cheeseburger --outputdir "$OUTPUT"
