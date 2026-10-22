#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
SOURCE=/home/dinnerabc.guest/unixnu-p3/diagnostic
OUTPUT=/project/UniXNU-cheeseburger/out/p4/ram-payload-uefi
bash scripts/p2-vm.sh start --tty=false p2
bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p4-prepare-auto-return.py
bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p4-prepare-rampayload.py
bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 bash scripts/p2-linux-build.sh "$SOURCE" "$OUTPUT"
bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p2-export.py "$SOURCE" "$OUTPUT"
bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p2-verify-images.py "$SOURCE" "$OUTPUT"
bash scripts/p2-vm.sh stop p2
