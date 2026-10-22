#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
sudo apt-get -o Acquire::http::Proxy=DIRECT -o Acquire::https::Proxy=DIRECT update
sudo apt-get -o Acquire::http::Proxy=DIRECT -o Acquire::https::Proxy=DIRECT install -y --no-install-recommends build-essential uuid-dev acpica-tools nasm gcc-aarch64-linux-gnu python3-distutils python3-pil python3-git gettext git zlib1g-dev ca-certificates file rsync
uname -a
gcc -dumpmachine
aarch64-linux-gnu-gcc -dumpmachine
aarch64-linux-gnu-gcc --version
python3 --version
iasl -v
nasm -v
make --version
dpkg-query -W -f='${Package}\t${Version}\t${Architecture}\n'
