#!/usr/bin/env bash
P3_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export ANDROID_USER_HOME="$P3_ROOT/.work/android"
export ADB_VENDOR_KEYS="$ANDROID_USER_HOME/adbkey"
export ADB_SERVER_SOCKET=tcp:localhost:5038
export TMPDIR="$P3_ROOT/.work/tmp"
export PATH="$P3_ROOT/.work/p3-tools-35/platform-tools:$PATH"

export ADB_LIBUSB=1
