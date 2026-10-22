#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bash "$ROOT/scripts/p4-build-xnu.sh"
python3 "$ROOT/scripts/p4-export-kernel.py"
