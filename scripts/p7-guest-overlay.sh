#!/usr/bin/env bash
set -euo pipefail
bash /project/UniXNU-cheeseburger/scripts/p6-guest-overlay.sh
BASE=/home/dinnerabc.guest/unixnu-p7
LOWER=/home/dinnerabc.guest/unixnu-p6/upper:/home/dinnerabc.guest/unixnu-p5/upper:/home/dinnerabc.guest/unixnu-p3/diagnostic
mkdir -p "$BASE"/{source,upper,work,build}
if ! mountpoint -q "$BASE/source"; then
 sudo mount -t overlay overlay -o "lowerdir=$LOWER,upperdir=$BASE/upper,workdir=$BASE/work" "$BASE/source"
fi
mkdir -p "$BASE/source/workspace/Build"
if ! mountpoint -q "$BASE/source/workspace/Build"; then
 sudo mount --bind "$BASE/build" "$BASE/source/workspace/Build"
fi
touch "$BASE/source/.p7-overlay-marker"
echo 'P7 independent writable overlay; P6/P5/P3 lower sources unchanged'
