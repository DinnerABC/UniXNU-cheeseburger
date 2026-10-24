#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/p4-env.sh"
CPU="$ROOT/out/p8/platform/libArmCpu.a"
PLATFORM="$ROOT/out/p4/arm-drivers/libArmDTPlatform.a"
CAPTURE="$ROOT/out/p14/capture/libP9Capture.a"
CRYPTO="$ROOT/.work/p4-unixnu/build/corecrypto-2023/libcorecrypto_kernel_noasm-namespaced.a"
PMGR="$ROOT/out/p11/pmgr/libCheeseburgerPlatformPower.a"
UFS="$ROOT/out/p12/native-ufs/libNativeUfs.a"
VERIFIER="$ROOT/out/p12/verifier/libVerifiedRoot.a"
POLICY="$ROOT/out/p12/policies/libVerifiedRootPolicy.a"
AUTH="$ROOT/out/p14/root-auth/libRootAuth.a"
for archive in "$AUTH" "$CPU" "$PLATFORM" "$CAPTURE" "$CRYPTO" "$PMGR" "$UFS" "$VERIFIER" "$POLICY"; do test -f "$archive"; done
KERNEL_EXTRA_ARCHIVES="-Wl,-force_load,$AUTH -L$ROOT/.work/p4-unixnu/.cache/fakeroot/usr/local/lib/kernel -lfirehose_kernel -Wl,-force_load,$CPU -Wl,-force_load,$PLATFORM -Wl,-force_load,$CAPTURE -Wl,-force_load,$PMGR -Wl,-force_load,$UFS -Wl,-force_load,$VERIFIER -Wl,-force_load,$POLICY $CRYPTO"
bash "$ROOT/scripts/p4-build-xnu.sh" "LD_KERNEL_ARCHIVES=$KERNEL_EXTRA_ARCHIVES" "XNU_EXTERNAL_KERNEL_ARCHIVES=$AUTH $CPU $PLATFORM $CAPTURE $PMGR $UFS $CRYPTO $VERIFIER $POLICY"
mkdir -p "$ROOT/out/p14/native"
python3 "$ROOT/scripts/p4-export-kernel.py" --source "$ROOT/.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/mach.development.msm8998.unstripped.noctf" --output "$ROOT/out/p14/native"
