#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
mkdir -p out/tmp
export TMPDIR="$ROOT/out/tmp"
export PYTHONDONTWRITEBYTECODE=1
if command -v brew >/dev/null; then
  for formula in llvm lld unifdef; do
    PATH="$(brew --prefix "$formula")/bin:$PATH"
  done
  PATH="$(brew --prefix make)/libexec/gnubin:$PATH"
  export PATH
fi
"${PYTHON:-python3}" scripts/p1.py package "$@" 2>&1 | tee out/package-console.log
