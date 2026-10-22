#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/p4-env.sh"
CPU="$ROOT/out/p4/arm-drivers/libArmCpu.a"
PLATFORM="$ROOT/out/p4/arm-drivers/libArmDTPlatform.a"
CRYPTO="$ROOT/.work/p4-unixnu/build/corecrypto-2023/libcorecrypto_kernel_noasm-namespaced.a"
BOOTSTRAP="$ROOT/out/p4/crypto-bootstrap/libP4CryptoBootstrap.a"
for P4_ARCHIVE in "$CPU" "$PLATFORM" "$CRYPTO" "$BOOTSTRAP"; do test -f "$P4_ARCHIVE"; done
bash "$ROOT/scripts/p4-build-xnu.sh" \
 "LD_KERNEL_ARCHIVES=-L$ROOT/.work/p4-unixnu/.cache/fakeroot/usr/local/lib/kernel -lfirehose_kernel -Wl,-force_load,$CPU -Wl,-force_load,$PLATFORM -Wl,-force_load,$BOOTSTRAP $CRYPTO" \
 "XNU_EXTERNAL_KERNEL_ARCHIVES=$CPU $PLATFORM $BOOTSTRAP $CRYPTO"
SOURCE="$ROOT/.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/mach.development.msm8998.unstripped.noctf"
mkdir -p "$ROOT/out/p6/kernel"
cp -p "$SOURCE" "$ROOT/out/p6/kernel/kernel.macho"
python3 "$ROOT/.work/p4-unixnu/scripts/seal-source-crypto-kernel.py" "$ROOT/out/p6/kernel/kernel.macho"
python3 "$ROOT/scripts/p4-export-kernel.py" --source "$ROOT/out/p6/kernel/kernel.macho" --output "$ROOT/out/p6/kernel"
