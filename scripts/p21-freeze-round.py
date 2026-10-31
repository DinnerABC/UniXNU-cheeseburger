#!/usr/bin/env python3
"""Freeze one candidate round into an immutable evidence directory.

The candidate tree (out/p21/fb-qup3) is overwritten every round, so the image,
kernel and gate/pins records no longer identify a past boot. This script copies
the artifacts and records hashes, args, gate result, pins scan, git state and
the boot/monitor/retrieval references under
out/p21/gpu-cp/rounds/<UTC>-<tag>/ without touching the device.

Historical rounds whose artifacts were already overwritten can be recorded with
--reconstruct <descriptor.json>; those manifests are marked reconstructed and
artifacts_present=false.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
ROUNDS = R / 'out/p21/gpu-cp/rounds'

# name -> candidate-relative path
ARTIFACTS = {
    'image': 'uefi/boot-p21-up1.img',
    'kernel': 'kernel/kernel.macho',
    'kernel_validation': 'kernel/validation.json',
    'adt': 'adt/kernel.macho.adt',
    'adt_validation': 'adt/validation.json',
    'firmware': 'uefi/MSM8998PKG_UEFI.fd',
    'dtb': 'uefi/Android-cheeseburger.dtb',
    'payload_container': 'payload/payload.container',
    'payload_bundle': 'payload/payload.bundle',
    'payload_manifest': 'payload/manifest.json',
    'boot_config': 'config/unixnu.toml',
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def entry(path):
    path = Path(path)
    return {'path': str(path.relative_to(R)), 'sha256': sha(path), 'bytes': path.stat().st_size}


def git(*args):
    return subprocess.run(['git', '-C', str(R), *args],
                          capture_output=True, text=True, check=False).stdout.strip()


def args_of(toml_text):
    m = re.search(r'^args = "(.*)"$', toml_text, re.M)
    return m.group(1) if m else None


def write_manifest(dest, manifest):
    dest.mkdir(parents=True, exist_ok=True)
    (dest / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'result': 'ROUND_FROZEN', 'dir': str(dest.relative_to(R))}))


def freeze(tag, candidate, copy_artifacts, boot_log, monitor_log, retrieval):
    C = R / candidate
    stamp = time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())
    dest = ROUNDS / f'{stamp}-{tag}'
    if dest.exists():
        sys.exit(f'refusing to overwrite {dest}')
    dest.mkdir(parents=True)
    hashes = {}
    for name, rel in ARTIFACTS.items():
        p = C / rel
        if not p.is_file():
            sys.exit(f'missing {p}')
        hashes[name] = entry(p)
    packaging = json.loads((C / 'uefi/packaging.json').read_text())
    if packaging.get('sha256') != hashes['image']['sha256']:
        sys.exit('packaging.json does not match the current image')
    args = args_of((C / 'config/unixnu.toml').read_text())
    if args is None:
        sys.exit('boot args missing from config/unixnu.toml')

    gates = sorted(C.glob('gate-*.json'))
    gate = json.loads(gates[-1].read_text()) if gates else None
    pins = C / 'logs/check-firmware-pins.log'
    pins_text = pins.read_text() if pins.is_file() else None

    evidence = {}
    for key, src in (('boot_log', boot_log), ('monitor_log', monitor_log), ('retrieval', retrieval)):
        if src:
            p = Path(src)
            if not p.is_absolute():
                p = R / p
            if not p.is_file():
                sys.exit(f'missing {key} {p}')
            evidence[key] = entry(p)
            name = {'boot_log': 'boot', 'monitor_log': 'monitor', 'retrieval': 'retrieval'}[key]
            shutil.copy2(p, dest / f'{name}{p.suffix}')
    boot_ids = []
    if evidence.get('retrieval'):
        info = json.loads(Path(retrieval).read_text())
        boot_ids = [f"0x{int(info['boot_id'], 0):08x}"] if info.get('boot_id') else []
        evidence['retrieval_summary'] = {k: info.get(k) for k in
                                         ('boot_id', 'rounds', 'write_errors', 'state', 'sha256')}

    if copy_artifacts:
        for name, rel in ARTIFACTS.items():
            shutil.copy2(C / rel, dest / f'{name}{Path(rel).suffix}')
    if gates:
        shutil.copy2(gates[-1], dest / gates[-1].name)
    if pins_text is not None:
        (dest / 'check-firmware-pins.log').write_text(pins_text)

    manifest = {
        'result': 'ROUND_FROZEN',
        'reconstructed': False,
        'artifacts_present': bool(copy_artifacts),
        'tag': tag,
        'frozen_utc': stamp,
        'candidate': candidate,
        'git_head': git('rev-parse', 'HEAD'),
        'git_dirty': [line for line in git('status', '--porcelain').splitlines() if line],
        'args': args,
        'args_bytes': len(args),
        'artifacts': hashes,
        'gate': gate,
        'pins_log': pins_text,
        'evidence': evidence,
        'boot_ids': boot_ids,
    }
    write_manifest(dest, manifest)


def reconstruct(descriptor_path):
    d = json.loads(Path(descriptor_path).read_text())
    dest = ROUNDS / d['dir']
    if dest.exists():
        sys.exit(f'refusing to overwrite {dest}')
    manifest = {
        'result': 'ROUND_RECONSTRUCTED',
        'reconstructed': True,
        'artifacts_present': False,
        'tag': d.get('tag'),
        'frozen_utc': d.get('frozen_utc') or time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()),
        'candidate': d.get('candidate', 'out/p21/fb-qup3'),
        'git_head': d.get('git_head'),
        'git_dirty': None,
        'args': d.get('args'),
        'args_bytes': d.get('args_bytes'),
        'artifacts': d.get('artifacts', {}),
        'gate': d.get('gate'),
        'pins_log': None,
        'evidence': d.get('evidence', {}),
        'boot_ids': d.get('boot_ids', []),
        'notes': d.get('notes', 'Hashes recorded during the round; artifacts were overwritten before freezing.'),
    }
    write_manifest(dest, manifest)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tag', nargs='?')
    ap.add_argument('--candidate', default='out/p21/fb-qup3')
    ap.add_argument('--no-copy', action='store_true')
    ap.add_argument('--boot-log')
    ap.add_argument('--monitor-log')
    ap.add_argument('--retrieval')
    ap.add_argument('--reconstruct')
    a = ap.parse_args()
    if a.reconstruct:
        reconstruct(a.reconstruct)
        return
    if not a.tag:
        sys.exit('tag or --reconstruct required')
    freeze(a.tag, a.candidate, not a.no_copy, a.boot_log, a.monitor_log, a.retrieval)


if __name__ == '__main__':
    main()
