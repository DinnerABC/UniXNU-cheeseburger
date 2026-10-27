#!/usr/bin/env python3
"""Apply the UniXNU patch series to a pristine xnu-8796.141.3 tree, strictly.

The 10063 series is routed through the Stage C overlay: a corrected patch in
patches-8796/ replaces its original, and the three obsolete patches listed in
drops.json are skipped.  Application uses patch(1) without fuzz so a silently
misplaced hunk cannot pass.

The tree is only written when --tree points at a scratch extraction.  This
script never touches the phone or the project out/p23 build outputs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess

R = Path(__file__).resolve().parents[4]
STAGE = R / 'out/p23/ios-16.7.16-20H392'
OVERLAY = STAGE / 'patches-8796'
SERIES = R / 'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/xnu-patches.list'
BOARD = R / 'patches/xnu'
DROPS = OVERLAY / 'drops.json'
REPORT = OVERLAY / 'apply-report.json'


def overlay_for(name):
    for group in ('xnu', 'board'):
        candidate = OVERLAY / group / name
        if candidate.is_file():
            return candidate
    return None


def resolved_series():
    rows = []
    for line in SERIES.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        original = Path(line)
        if not original.is_file():
            raise SystemExit('series entry missing: ' + line)
        overlay = overlay_for(original.name)
        rows.append({'name': original.name, 'original': original,
                     'patch': overlay or original, 'overlay': overlay is not None})
    return rows


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tree', required=True)
    parser.add_argument('--commit-each', action='store_true')
    args = parser.parse_args()
    tree = Path(args.tree).resolve()
    if not (tree / 'makedefs/MakeInc.kernel').is_file():
        raise SystemExit('%s is not an xnu source tree' % tree)
    if STAGE in tree.parents or tree == STAGE:
        raise SystemExit('refusing to patch inside the project stage directory')

    drops = json.loads(DROPS.read_text()) if DROPS.is_file() else {}
    if args.commit_each:
        subprocess.run(['git', 'init', '-q'], cwd=tree, check=True)
        subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
        subprocess.run(['git', '-c', 'user.email=stage-c@local', '-c', 'user.name=stage-c',
                        'commit', '-qm', 'base xnu-8796.141.3'], cwd=tree, check=True)

    rows = []
    for row in resolved_series():
        name = row['name']
        if name in drops:
            rows.append({'patch': name, 'result': 'DROPPED', 'reason': drops[name]})
            print('DROP      ' + name, flush=True)
            continue
        run = subprocess.run(['patch', '-p1', '-V', 'none', '--batch', '--forward', '-N'],
                             cwd=tree, input=row['patch'].read_bytes(), capture_output=True)
        log = (run.stdout + run.stderr).decode(errors='replace')
        failed = re.findall(r'Hunk #(\d+) failed', log, re.I)
        fuzz = re.findall(r'Hunk #(\d+) succeeded at .*with fuzz', log, re.I)
        result = 'CLEAN' if run.returncode == 0 else ('PARTIAL' if failed else 'FAILED')
        rows.append({'patch': name, 'result': result,
                     'overlay': row['overlay'],
                     'source': str(row['patch'].relative_to(R)),
                     'source_sha256': sha256(row['patch']),
                     'failed_hunks': [int(n) for n in failed],
                     'fuzzed_hunks': [int(n) for n in fuzz],
                     'log_tail': log[-1500:] if result != 'CLEAN' else ''})
        print('%-9s %s%s' % (result, name, ' [overlay]' if row['overlay'] else ''), flush=True)
        if args.commit_each:
            subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
            subprocess.run(['git', '-c', 'user.email=stage-c@local', '-c', 'user.name=stage-c',
                            'commit', '-qm', name, '--allow-empty'], cwd=tree, check=True)
        if result == 'FAILED':
            break

    summary = {}
    for row in rows:
        summary[row['result']] = summary.get(row['result'], 0) + 1
    report = {'tree': str(tree), 'base': 'xnu-8796.141.3', 'summary': summary,
              'overlay': str(OVERLAY.relative_to(R)), 'patches': rows}
    print(json.dumps(summary, indent=2))
    if len(rows) == len(list(resolved_series())):
        REPORT.write_text(json.dumps(report, indent=2) + '\n')
    return 0 if summary.get('CLEAN', 0) + summary.get('DROPPED', 0) == len(rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
