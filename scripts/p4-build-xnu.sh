#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/p4-env.sh"
U="$ROOT/.work/p4-unixnu"
export KERNEL_SDKROOT="$U/.cache/fakeroot"
export XNU_AVAILABILITY_PL="$KERNEL_SDKROOT/usr/local/libexec/availability.pl"
[[ -x "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/iig" ]]
exec make -C "$U/.cache/xnu-work" "${P4_XNU_TARGET:-build}" \
  SDKROOT=iphoneos ARCH_CONFIGS=ARM64 MACHINE_CONFIGS=MSM8998 KERNEL_CONFIGS=DEVELOPMENT \
  ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 LARGE_MEMORY_DEFINE=-UARM_LARGE_MEMORY \
  ARM64_LINK_ADDR=0xfffffff007004000 ARM64_PLKSEG_ADDR=0xfffffff004004000 \
  DEPLOYMENT_TARGET_FLAGS='-miphoneos-version-min=17.0 -DXNU_TARGET_OS_IOS' \
  DEPLOYMENT_LINKER_FLAGS='-Wl,-ios_version_min,17.0' \
  RC_DARWIN_KERNEL_VERSION=23.6.0 RC_ProjectSourceVersion=10063.141.1 \
  CFLAGS_EXTRA='-Wno-reserved-identifier -Wno-pre-c11-compat -Wno-missing-designated-field-initializers -Wno-suggest-override -Wno-unnecessary-virtual-specifier -Wno-suggest-destructor-override -Wno-ms-bitfield-padding' \
  BUILD_LTO=0 BOUND_CHECKS=0 BUILD_WERROR=0 MAKEJOBS=-j2 \
  OBJROOT="$U/build/xnu/ios-obj" SYMROOT="$U/build/xnu/ios-sym" DSTROOT="$U/build/xnu/ios-dst" "$@"
