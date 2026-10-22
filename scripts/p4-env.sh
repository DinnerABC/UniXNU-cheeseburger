#!/usr/bin/env bash
P4_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export TMPDIR="$P4_ROOT/.work/tmp"
export PYTHONPYCACHEPREFIX="$P4_ROOT/.work/p4/pycache"
export PYTHONDONTWRITEBYTECODE=1
export PYTHONUTF8=1
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:/opt/homebrew/opt/unifdef/bin:$PATH"
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
