#!/usr/bin/env python3
"""Build the owner userland probe, its launchd jobs, the NGPUADD2 GPU addon and
the owner trust bundle for one P21 candidate.

The addon is written into the Data volume by the kernel's boot-time addon mount
(sheng-gpu-addon=1); the trust bundle replaces the payload's trustcache and adds
the daemon and plugin CDHashes as one engineering module after the two unchanged
Apple modules. Nothing is written to the Data seed or to the OS inputs.
"""
from pathlib import Path
import hashlib
import json
import plistlib
import os
import shutil
import struct
import subprocess
import sys
import uuid
import zlib

ENV = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
R = Path(__file__).resolve().parents[1]
C = R / 'out/p21/atos-gate'
OUT = Path(os.environ.get('P21_PROBE_OUT', C / 'userland'))
SRC = R / 'package/Userland/adrenoprobed.cpp'
ABI = R / 'package/BoardRuntime/NativeAdrenoABI.h'
ORIGINAL = R / 'out/p12/trust/original-static-system.bundle'
ORIGINAL_SHA = '7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7'
DAEMON_ID = 'org.cheeseburger.adrenoprobed'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sdk():
    return subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True, env=ENV).strip()


def pad8(data):
    return data + bytes(-len(data) % 8)


def valid_like_loader(data):
    count = struct.unpack_from('<I', data)[0]
    if not 0 < count <= 128:
        return False
    for index in range(count):
        start = struct.unpack_from('<I', data, 4 + 4 * index)[0]
        end = struct.unpack_from('<I', data, 8 + 4 * index)[0] if index + 1 < count else len(data)
        if start < 4 + 4 * count or start & 3 or end > len(data) or end - start < 24 or struct.unpack_from('<I', data, start)[0] != 2:
            return False
        entries = struct.unpack_from('<I', data, start + 20)[0]
        if end - start != 24 + entries * 24:
            return False
        for entry in range(entries):
            at = start + 24 + entry * 24
            if not 1 <= data[at + 20] <= 4 or (entry and data[at - 24:at - 4] >= data[at:at + 20]):
                return False
    return True


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    entitlements = OUT / 'adrenoprobed-entitlements.plist'
    entitlements.write_bytes(plistlib.dumps({
        'com.apple.private.security.no-container': True,
        'com.apple.security.iokit-user-client-class': ['AGXDeviceUserClient'],
    }))
    daemon = OUT / 'adrenoprobed'
    subprocess.run(['xcrun', 'clang++', '-target', 'arm64-apple-ios17.0',
                    '-isysroot', sdk(), '-std=c++17', '-O2', '-fno-exceptions', '-fno-rtti',
                    '-I', str(R / 'package/BoardRuntime'), str(SRC), '-framework', 'IOKit',
                    '-o', str(daemon)], check=True, env=ENV)
    subprocess.run(['codesign', '--force', '--sign', '-', '--pagesize', '4096',
                    '--identifier', DAEMON_ID, '--entitlements', str(entitlements), str(daemon)], check=True, env=ENV)
    subprocess.run(['codesign', '--verify', '--strict', str(daemon)], check=True)
    detail = subprocess.run(['codesign', '--display', '--verbose=4', str(daemon)],
                            capture_output=True, text=True, check=True, env=ENV).stderr
    cdhash = None
    for line in detail.splitlines():
        if line.startswith('CDHash='):
            cdhash = line.split('=', 1)[1]
    if not cdhash or len(cdhash) != 40:
        sys.exit('no CDHash')
    if 'Page size=4096' not in detail:
        sys.exit('the signature must use 4 KiB pages')

    # Owner Metal plugin: the original discovery loads this bundle inside
    # backboardd (IOAcceleratorES -> MetalPluginName -> NSBundle -> principal
    # class -> initWithAcceleratorPort:). It needs its own trusted CDHash.
    plugin_bundle = OUT / 'NativeAdrenoMetal.bundle'
    shutil.rmtree(plugin_bundle, ignore_errors=True)
    plugin_bundle.mkdir(parents=True)
    plugin_exe = plugin_bundle / 'NativeAdrenoMetal'
    probe_flags=['-DNATIVE540_ENCODER_ONLY=1']
    if os.environ.get('P21_PROBE_GLOBAL')=='1':probe_flags.append('-DNATIVE540_PROBE_GLOBAL=1')
    subprocess.run(['xcrun', 'clang++', '-target', 'arm64-apple-ios17.0',
                    '-isysroot', sdk(), '-std=c++17', '-fobjc-arc', '-O2', '-fno-exceptions',
                    *probe_flags,
                    '-fno-rtti', '-I', str(R / 'package/BoardRuntime'),
                    '-I', str(R / 'package/Userland'),
                    '-bundle', str(R / 'package/Userland/native_adreno_metal_plugin.mm'),
                    str(R / 'package/Userland/NativeMetal540Client.cpp'),
                    str(R / 'package/Userland/NativeMetal540Probe.cpp'),
                    str(R / 'package/Userland/NativeMetal540RuntimeABI.mm'),
                    str(R / 'package/Userland/NativeMetal540Transform.cpp'),
                    str(R / 'package/Userland/NativeMetal540AIRProbe.cpp'),
                    str(R / 'package/Userland/NativeMetal540Objects.mm'),
                    str(R / 'package/Userland/NativeMetal540OwnedRun.mm'),
                    str(R / 'package/Userland/NativeMetal540GraphicsOwnedRun.mm'),
                    str(R / 'package/Userland/NativeMetal540Capabilities.mm'),
                    str(R / 'package/Userland/NativeMetal540DeviceAdapter.mm'),
                    str(R / 'package/Userland/NativeMetal540ProtocolAudit.mm'),
                    str(R / 'package/Userland/NativeMetal540Render.cpp'),
                    str(R / 'package/Userland/NativeMetal540RenderAIRProbe.cpp'),
                    str(R / 'package/Userland/NativeMetal540Surface.mm'),
                    '-framework', 'Foundation', '-framework', 'IOKit', '-framework', 'Metal', '-framework', 'IOSurface', '-o', str(plugin_exe)],
                   check=True, env=ENV)
    subprocess.run(['strip', '-x', str(plugin_exe)], check=True, env=ENV)
    (plugin_bundle / 'Info.plist').write_bytes(plistlib.dumps(dict(
        CFBundleIdentifier='org.cheeseburger.NativeAdrenoMetal', CFBundleExecutable='NativeAdrenoMetal',
        CFBundlePackageType='BNDL', CFBundleVersion='1', MinimumOSVersion='17.0',
        NSPrincipalClass='NativeAdrenoMetalDevice'), fmt=plistlib.FMT_XML))
    subprocess.run(['codesign', '--force', '--sign', '-', '--pagesize', '4096',
                    '--identifier', 'org.cheeseburger.NativeAdrenoMetal', str(plugin_bundle)],
                   check=True, env=ENV)
    subprocess.run(['codesign', '--verify', '--strict', str(plugin_bundle)], check=True)
    plugin_detail = subprocess.run(['codesign', '--display', '--verbose=4', str(plugin_bundle)],
                                   capture_output=True, text=True, check=True, env=ENV).stderr
    plugin_cdhash = None
    for line in plugin_detail.splitlines():
        if line.startswith('CDHash='):
            plugin_cdhash = line.split('=', 1)[1]
    if not plugin_cdhash or len(plugin_cdhash) != 40:
        sys.exit('no plugin CDHash')
    if 'Page size=4096' not in plugin_detail:
        sys.exit('the plugin signature must use 4 KiB pages')

    jobs = {
        DAEMON_ID: dict(Label=DAEMON_ID, ProgramArguments=['/private/var/mobile/ngpu/bin/adrenoprobed'],
                        RunAtLoad=True, KeepAlive=False, UserName='root', ThrottleInterval=30,
                        LimitLoadToSessionType='System',
                        ProcessType='Interactive', StandardErrorPath='/dev/null'),
    }
    for name, job in jobs.items():
        (OUT / (name + '.plist')).write_bytes(plistlib.dumps(job, fmt=plistlib.FMT_XML))

    files = [('bin/adrenoprobed', 0o755, daemon.read_bytes())]
    for path in sorted(plugin_bundle.rglob('*')):
        if path.is_file() and not path.is_symlink():
            relative=path.relative_to(plugin_bundle).as_posix()
            mode=0o755 if relative=='NativeAdrenoMetal' else 0o644
            files.append(('addons/NativeAdrenoMetal.bundle/'+relative,mode,path.read_bytes()))
    for name in sorted(jobs):
        files.append((f'daemons/{name}.plist', 0o644, (OUT / (name + '.plist')).read_bytes()))
    entries = []
    for path, mode, data in files:
        raw = path.encode() + b'\0'
        if len(raw) > 160:
            sys.exit('addon path too long: ' + path)
        packed = zlib.compress(data, 9)
        entries.append(struct.pack('<IIQQ', len(raw), mode, len(data), len(packed)) + pad8(raw) + pad8(packed))
    body = b''.join(entries)
    addon = struct.pack('<8sIIQ', b'NGPUADD2', len(files), 0, 24 + len(body)) + body
    (OUT / 'kernel.macho.gpuaddon').write_bytes(addon)

    original = ORIGINAL.read_bytes()
    if sha(original) != ORIGINAL_SHA:
        sys.exit('original trust bundle differs from the pinned input')
    count = struct.unpack_from('<I', original)[0]
    offsets = struct.unpack_from('<2I', original, 4)
    if count != 2:
        sys.exit('the pinned bundle must carry exactly two Apple modules')
    originals = [original[offsets[0]:offsets[1]], original[offsets[1]:]]
    hashes = sorted([bytes.fromhex(cdhash), bytes.fromhex(plugin_cdhash)])
    own = struct.pack('<I16sI', 2,
                      uuid.uuid5(uuid.NAMESPACE_URL, 'org.cheeseburger.adreno:' + cdhash).bytes,
                      len(hashes)) + b''.join(h + bytes([2, 0, 0, 0]) for h in hashes)
    # The kernel loads module 0 as the static trust cache and panics if it
    # fails (bsd/kern/kern_trustcache.c). Keep the two unchanged Apple modules
    # first; the owner module is then an engineering module, whose failure is
    # reported and skipped instead of panicking before the log writer starts.
    modules = [*originals, own]
    cursor, starts = 16, []
    for module in modules:
        starts.append(cursor)
        cursor += len(module)
    bundle = struct.pack('<4I', len(modules), *starts) + b''.join(modules)
    if not valid_like_loader(bundle):
        sys.exit('the trust bundle would be refused by the boot loader')
    (OUT / 'kernel.macho.trustcache').write_bytes(bundle)

    report = dict(result='ADRENOPROBED_ADDON_BUILD_PASS', daemon_sha256=sha(daemon.read_bytes()),
                  daemon_cdhash=cdhash, entitlements=sha(entitlements.read_bytes()),
                  addon_bytes=len(addon), addon_sha256=sha(addon), addon_files=[f[0] for f in files],
                  trustcache_bytes=len(bundle), trustcache_sha256=sha(bundle),
                  plugin_sha256=sha(plugin_exe.read_bytes()), plugin_cdhash=plugin_cdhash,
                  plugin_bundle='NativeAdrenoMetal.bundle', plugin_class='NativeAdrenoMetalDevice',
                  trust_own_cdhashes=[cdhash, plugin_cdhash], trust_original_modules_unchanged=True,
                  probe_profile='globalID-four-groups-256' if os.environ.get('P21_PROBE_GLOBAL')=='1' else 'localID-one-group-64',
                  probe_fixture_sha256=sha((R/'tools/gpu-native540'/('probe_global_fixture.h' if os.environ.get('P21_PROBE_GLOBAL')=='1' else 'probe_fixture.h')).read_bytes()),
                  hardware_executed=False)
    (OUT / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
