#!/usr/bin/env bash
set -euo pipefail
[[ $(uname -s) == Linux ]]
BASE=/home/dinnerabc.guest/unixnu-p5
LOWER=/home/dinnerabc.guest/unixnu-p3/diagnostic
mkdir -p "$BASE"/{source,upper,work,build}
if ! mountpoint -q "$BASE/source"; then
  test -f "$LOWER/P4Integration/Probe/P4Probe.inf"
  sudo mount -t overlay overlay -o "lowerdir=$LOWER,upperdir=$BASE/upper,workdir=$BASE/work" "$BASE/source"
fi
# The old build artifacts are read-only lower data; P5 gets an independent build dir.
mkdir -p "$BASE/source/workspace/Build"
if ! mountpoint -q "$BASE/source/workspace/Build"; then
  sudo mount --bind "$BASE/build" "$BASE/source/workspace/Build"
fi
touch "$BASE/source/.p5-overlay-marker"
echo 'P5 overlay mounted; P3/P4 lower tree unchanged; separate P5 build output'
