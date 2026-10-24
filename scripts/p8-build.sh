#!/usr/bin/env bash
# Reproduce one P8 candidate. No device operations and no partition writes.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
source scripts/p4-env.sh
P8_LABEL=${P8_LABEL:-rebuild-$(date -u +%Y%m%dT%H%M%SZ)}
P8_PY=/Users/dinnerabc/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3
run() { python3 scripts/p8run.py "$P8_LABEL-$1" -- "${@:2}"; }
run inputs python3 scripts/p8-validate-inputs.py
# VZ must really be stopped before the kernel build, not inferred from a
# sandboxed limactl failure. Run with normal local VM access.
bash scripts/p2-vm.sh stop p2
run storage python3 scripts/p8-build-storage.py
run platform python3 scripts/p8-build-platform.py
run ufs python3 scripts/p8-build-native.py
run ramblock python3 scripts/p8-build-ramblock.py
run prepare python3 scripts/p8-prepare-kernel.py
run adt python3 scripts/p8-build-adt.py
run adt-parser python3 scripts/p8-check-adt.py out/p8/adt/kernel.macho.adt
run interrupt-contract python3 scripts/p8-test-interrupt-contract.py
run kernel bash scripts/p8-link-kernel.sh
run integration python3 scripts/p8-check-integration.py
run payload python3 scripts/p8-build-payload.py
bash scripts/p2-vm.sh start p2
run overlay bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 bash scripts/p8-guest-overlay.sh
run uefi-prepare bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p8-prepare-uefi.py
run uefi-build bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 bash scripts/p2-linux-build.sh /home/dinnerabc.guest/unixnu-p8/source /project/UniXNU-cheeseburger/out/p8/uefi
run uefi-export bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p2-export.py /home/dinnerabc.guest/unixnu-p8/source out/p8/uefi
run uefi-verify bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 python3 scripts/p2-verify-images.py /home/dinnerabc.guest/unixnu-p8/source out/p8/uefi
run bootimg python3 scripts/p8-pack-image.py
run geometry "$P8_PY" scripts/p8-build-root.py --geometry-only
if [[ -n ${P8_ROOT_INPUTS:-} ]]; then
 run root "$P8_PY" scripts/p8-build-root.py --inputs "$P8_ROOT_INPUTS"
else
 echo 'ROOT BLOCKED: compatible authenticated Apple/APFS inputs not supplied; no root image generated.'
fi
bash scripts/p2-vm.sh stop p2
