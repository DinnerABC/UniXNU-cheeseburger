#!/usr/bin/env bash
set -euo pipefail
SOURCE=/home/dinnerabc.guest/unixnu-p9/source
BASE=/home/dinnerabc.guest/unixnu-p13
mkdir -p "$BASE"/{upper,work}
if [[ ! -e "$SOURCE/.p13-overlay-marker" ]]; then
 bash /project/UniXNU-cheeseburger/scripts/p12-guest-overlay.sh
 sudo umount "$SOURCE/workspace/Build"
 sudo umount "$SOURCE"
 LOWER=/home/dinnerabc.guest/unixnu-p12/upper:/home/dinnerabc.guest/unixnu-p11/upper:/home/dinnerabc.guest/unixnu-p10/upper:/home/dinnerabc.guest/unixnu-p9/upper:/home/dinnerabc.guest/unixnu-p8/upper:/home/dinnerabc.guest/unixnu-p7/upper:/home/dinnerabc.guest/unixnu-p6/upper:/home/dinnerabc.guest/unixnu-p5/upper:/home/dinnerabc.guest/unixnu-p3/diagnostic
 sudo mount -t overlay overlay -o "lowerdir=$LOWER,upperdir=$BASE/upper,workdir=$BASE/work" "$SOURCE"
 mkdir -p "$SOURCE/workspace/Build"
 sudo mount --bind /home/dinnerabc.guest/unixnu-p9/build "$SOURCE/workspace/Build"
 touch "$SOURCE/.p13-overlay-marker"
fi
echo 'P13 private upper; P12 sources preserved'
