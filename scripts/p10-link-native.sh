#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/p4-env.sh"
CPU="$ROOT/out/p8/platform/libArmCpu.a"
PLATFORM="$ROOT/out/p4/arm-drivers/libArmDTPlatform.a"
CRYPTO="$ROOT/.work/p4-unixnu/build/corecrypto-2023/libcorecrypto_kernel_noasm-namespaced.a"
CAPTURE="$ROOT/out/p9/capture-v2/libP9Capture.a"
# Original donor corecrypto and IOStorageFamily are in the real dependency
# closure. Do not also register the P4 source provider or duplicate storage
# C++ metaclasses. Source crypto primitives remain available for early_random.
# NativeUfs must be bound to the original storage ABI before it is re-enabled.
for archive in "$CPU" "$PLATFORM" "$CRYPTO" "$CAPTURE"; do test -f "$archive"; done
bash "$ROOT/scripts/p4-build-xnu.sh" \
 "LD_KERNEL_ARCHIVES=-L$ROOT/.work/p4-unixnu/.cache/fakeroot/usr/local/lib/kernel -lfirehose_kernel -Wl,-force_load,$CPU -Wl,-force_load,$PLATFORM -Wl,-force_load,$CAPTURE $CRYPTO" \
 "XNU_EXTERNAL_KERNEL_ARCHIVES=$CPU $PLATFORM $CAPTURE $CRYPTO"
mkdir -p "$ROOT/out/p10/native"
cp -p "$ROOT/.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/mach.development.msm8998.unstripped.noctf" "$ROOT/out/p10/native/kernel.macho"
python3 "$ROOT/scripts/p4-export-kernel.py" --source "$ROOT/out/p10/native/kernel.macho" --output "$ROOT/out/p10/native"
