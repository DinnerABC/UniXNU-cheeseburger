#!/bin/bash
# Wrap MIG: generate normally, then add the iOS16 mach_ports_register request layout
# to the generated task_server.c. Used by the P23 kernel build via MIG=...
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
server=""
prev=""
for arg in "$@"; do
  if [ "$prev" = "-server" ]; then server="$arg"; fi
  prev="$arg"
done
/usr/bin/mig "$@"
rc=$?
if [ "$rc" -eq 0 ] && [ -n "$server" ] && [ -f "$server" ]; then
  /opt/homebrew/opt/python@3.13/bin/python3.13 "$R/scripts/p23-patch-task-server.py" "$server" || exit 1
  /opt/homebrew/opt/python@3.13/bin/python3.13 "$R/scripts/p23-patch-task-server-lookup.py" "$server" || exit 1
fi
exit "$rc"
