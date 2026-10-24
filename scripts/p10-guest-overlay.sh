#!/usr/bin/env bash
set -euo pipefail
SOURCE=/home/dinnerabc.guest/unixnu-p9/source
BASE=/home/dinnerabc.guest/unixnu-p10
mkdir -p "$BASE"/{upper,work}
if [[ ! -e "$SOURCE/.p10-overlay-marker" ]]; then
 bash /project/UniXNU-cheeseburger/scripts/p9-guest-overlay.sh
 sudo umount "$SOURCE/workspace/Build"
 sudo umount "$SOURCE"
 LOWER=/home/dinnerabc.guest/unixnu-p9/upper:/home/dinnerabc.guest/unixnu-p8/upper:/home/dinnerabc.guest/unixnu-p7/upper:/home/dinnerabc.guest/unixnu-p6/upper:/home/dinnerabc.guest/unixnu-p5/upper:/home/dinnerabc.guest/unixnu-p3/diagnostic
 sudo mount -t overlay overlay -o "lowerdir=$LOWER,upperdir=$BASE/upper,workdir=$BASE/work" "$SOURCE"
 mkdir -p "$SOURCE/workspace/Build"
 sudo mount --bind /home/dinnerabc.guest/unixnu-p9/build "$SOURCE/workspace/Build"
 touch "$SOURCE/.p10-overlay-marker"
fi
echo 'P10 private upper; original P9 upper untouched; identical WORKSPACE path reuses UEFI object cache'
