#!/usr/bin/env bash
set -euo pipefail
SOURCE=/home/dinnerabc.guest/unixnu-p9/source
BASE=/home/dinnerabc.guest/unixnu-p21p21up1
mkdir -p "$BASE"/{upper,work}
if [[ ! -e "$SOURCE/.p21p21up1-overlay-marker" ]]; then
 # Never run an earlier phase pin writer against its frozen overlay.
 if mountpoint -q "$SOURCE/workspace/Build"; then sudo umount "$SOURCE/workspace/Build"; fi
 if mountpoint -q "$SOURCE"; then sudo umount "$SOURCE"; fi
 mkdir -p "$SOURCE"
 LOWER=/home/dinnerabc.guest/unixnu-p21r17diag2/upper:/home/dinnerabc.guest/unixnu-p21r17diag1/upper:/home/dinnerabc.guest/unixnu-p21r17/upper:/home/dinnerabc.guest/unixnu-p21r16/upper:/home/dinnerabc.guest/unixnu-p21r15/upper:/home/dinnerabc.guest/unixnu-p21r14/upper:/home/dinnerabc.guest/unixnu-p21r13/upper:/home/dinnerabc.guest/unixnu-p21r12/upper:/home/dinnerabc.guest/unixnu-p21r11/upper:/home/dinnerabc.guest/unixnu-p21r10/upper:/home/dinnerabc.guest/unixnu-p21r9/upper:/home/dinnerabc.guest/unixnu-p21official/upper:/home/dinnerabc.guest/unixnu-p21r7/upper:/home/dinnerabc.guest/unixnu-p21r6/upper:/home/dinnerabc.guest/unixnu-p21r5/upper:/home/dinnerabc.guest/unixnu-p21r4/upper:/home/dinnerabc.guest/unixnu-p21r3/upper:/home/dinnerabc.guest/unixnu-p21r2/upper:/home/dinnerabc.guest/unixnu-p21r1/upper:/home/dinnerabc.guest/unixnu-p20r14/upper:/home/dinnerabc.guest/unixnu-p20r13/upper:/home/dinnerabc.guest/unixnu-p20r12/upper:/home/dinnerabc.guest/unixnu-p20r11/upper:/home/dinnerabc.guest/unixnu-p20r10/upper:/home/dinnerabc.guest/unixnu-p20r9/upper:/home/dinnerabc.guest/unixnu-p20r8/upper:/home/dinnerabc.guest/unixnu-p20r7/upper:/home/dinnerabc.guest/unixnu-p20r6/upper:/home/dinnerabc.guest/unixnu-p20r5/upper:/home/dinnerabc.guest/unixnu-p20r4/upper:/home/dinnerabc.guest/unixnu-p20r3/upper:/home/dinnerabc.guest/unixnu-p20r2/upper:/home/dinnerabc.guest/unixnu-p20/upper:/home/dinnerabc.guest/unixnu-p19r2/upper:/home/dinnerabc.guest/unixnu-p19/upper:/home/dinnerabc.guest/unixnu-p18/upper:/home/dinnerabc.guest/unixnu-p17r4/upper:/home/dinnerabc.guest/unixnu-p17r3/upper:/home/dinnerabc.guest/unixnu-p17r2/upper:/home/dinnerabc.guest/unixnu-p17/upper:/home/dinnerabc.guest/unixnu-p16/upper:/home/dinnerabc.guest/unixnu-p15/upper:/home/dinnerabc.guest/unixnu-p14/upper:/home/dinnerabc.guest/unixnu-p13/upper:/home/dinnerabc.guest/unixnu-p12/upper:/home/dinnerabc.guest/unixnu-p11/upper:/home/dinnerabc.guest/unixnu-p10/upper:/home/dinnerabc.guest/unixnu-p9/upper:/home/dinnerabc.guest/unixnu-p8/upper:/home/dinnerabc.guest/unixnu-p7/upper:/home/dinnerabc.guest/unixnu-p6/upper:/home/dinnerabc.guest/unixnu-p5/upper:/home/dinnerabc.guest/unixnu-p3/diagnostic
 sudo mount -t overlay overlay -o "lowerdir=$LOWER,upperdir=$BASE/upper,workdir=$BASE/work" "$SOURCE"
 mkdir -p "$SOURCE/workspace/Build"
 sudo mount --bind /home/dinnerabc.guest/unixnu-p9/build "$SOURCE/workspace/Build"
 touch "$SOURCE/.p21p21up1-overlay-marker"
fi

