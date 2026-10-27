#!/usr/bin/env python3
"""Build the Stage C iPhone X (D22AP / iPhone10,3) ADT identity.

Hardware base: the P23 2 GiB candidate ADT (out/p23/ios-16.7.11/adt/kernel.macho.adt).
Only identity/root-matching properties change and one top-level /buttons node is added;
MSM8998 CPU/UFS/PSCI/display nodes, /chosen memory map, /defaults and /options are
kept byte-for-byte.

The base candidate is only read. Nothing under out/p23/ios-16.7.11/ is written and
existing Stage C outputs are never overwritten.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import uuid

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/adt'))
from adt import decode, encode  # noqa: E402

STAGE = ROOT / 'out/p23/ios-16.7.16-20H392'
BASE = ROOT / 'out/p23/ios-16.7.11/adt/kernel.macho.adt'
BASE_SHA256 = '1092cd294d1759fb2c0054613f77ae5682bef3a3f190a6184def62c3f2293754'
BASE_BYTES = 14260
OUT_DIR = STAGE / 'adt'
OUT = OUT_DIR / 'kernel.macho.adt'
REPORT = OUT_DIR / 'validation.json'

# 20H392 OS root APFS volume (System role), see policies/OS/manifest.json.
OLD_ROOT_UUID = 'E652776C-E75D-4712-850F-AB4D81C3D836'
ROOT_UUID = 'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7'
ROOT_MATCHING = ('<dict><key>IOProviderClass</key><string>IOMedia</string>'
                 '<key>IOPropertyMatch</key><dict><key>UUID</key><string>%s</string>'
                 '</dict></dict>\0')

IDENTITY = {
    'model': b'iPhone10,3\0',
    'target-type': b'D22\0',
    'target-sub-type': b'D22AP\0',
    'product-name': b'iPhone X\0',
    'product-description': b'iPhone X\0',
    'artwork-device-idiom': b'phone\0',
    'compatible-device-fallback': b'iPhone9,2\0',
}
BUTTON_NAMES = ['volup', 'voldown', 'hold', 'ringerab']
# /buttons/button-names in the 20H392 D22AP donor DTB: NUL-separated C strings.
DONOR_BUTTON_NAMES = b'volup\0voldown\0hold\0ringerab\0'
DONOR_DTB_SHA256 = '9e670987c80493bd69aba7be62e3179c04663324d9249b0973675baac175dbf1'
IDENTITY_JSON_SHA256 = '192e9877744b4bceb940e9540d473f9edc4584d25c4df36fbc2d31d991f4d061'

CHANGED = [
    '/model',
    '/target-type',
    '/target-sub-type',
    '/chosen/root-matching',
    '/product/product-name',
    '/product/product-description',
    '/product/artwork-device-idiom',
    '/product/artwork-scale-factor',
    '/product/artwork-device-subtype',
    '/product/compatible-device-fallback',
    '/buttons/name',
    '/buttons/compatible',
    '/buttons/device_type',
    '/buttons/button-names',
]
# The only allowed node-level addition is this top-level node.
BUTTONS_NODE = {
    'name': 'buttons',
    'properties': {
        'name': b'buttons\0',
        'compatible': b'buttons\0',
        'device_type': b'buttons\0',
        'button-names': DONOR_BUTTON_NAMES,
    },
    'children': [],
}
HARDWARE_NODES = ['cpus', 'arm-io', 'defaults', 'options', 'psci']


def emit(node):
    return encode(node['name'],
                  {k: v for k, v in node['properties'].items() if k != 'name'},
                  [emit(child) for child in node['children']])


def child(node, name):
    found = [c for c in node['children'] if c['name'] == name]
    if len(found) != 1:
        raise AssertionError('%s: expected exactly one /%s child' % (node['name'], name))
    return found[0]


def diff(old, new, path='', allow_new=()):
    """Every property addition, removal or value change, as (path/prop, before, after).

    `allow_new` is only honored at the level it is passed for (the root): it permits
    exactly those child nodes to be appended, and reports each of their properties.
    """
    out = []
    here = path or ''
    old_props, new_props = old['properties'], new['properties']
    for key in old_props:
        if key not in new_props:
            out.append(('%s/%s' % (here, key), old_props[key], None))
    for key in new_props:
        if key not in old_props:
            out.append(('%s/%s' % (here, key), None, new_props[key]))
        elif new_props[key] != old_props[key]:
            out.append(('%s/%s' % (here, key), old_props[key], new_props[key]))
    old_names = [c['name'] for c in old['children']]
    new_names = [c['name'] for c in new['children']]
    added = [c for c in new_names if c not in old_names]
    if new_names != old_names:
        kept = [c for c in new_names if c in old_names]
        if added != list(allow_new) or kept != old_names:
            raise AssertionError('node set changed at %s: %r -> %r'
                                 % (here or '/', old_names, new_names))
        for extra in new['children']:
            if extra['name'] not in added:
                continue
            if extra['children']:
                raise AssertionError('added node /%s must be a leaf' % extra['name'])
            for key, value in extra['properties'].items():
                out.append(('%s/%s/%s' % (here, extra['name'], key), None, value))
    for old_child, new_child in zip(old['children'],
                                    [c for c in new['children'] if c['name'] in old_names]):
        out.extend(diff(old_child, new_child, '%s/%s' % (here, old_child['name'])))
    return out


def u32(value):
    return struct.pack('<I', value)


def insert_after(properties, anchor, key, value):
    if key in properties:
        raise AssertionError('refusing to overwrite existing %s' % key)
    rebuilt = {}
    for name, current in properties.items():
        rebuilt[name] = current
        if name == anchor:
            rebuilt[key] = value
    if key not in rebuilt:
        raise AssertionError('anchor %s missing' % anchor)
    return rebuilt


def main():
    if sys.argv[1:]:
        raise SystemExit('usage: %s (takes no arguments)' % sys.argv[0])
    names = DONOR_BUTTON_NAMES

    for target in (OUT, REPORT):
        if target.exists():
            raise SystemExit('%s already exists; previous candidates are preserved' % target)
    if STAGE not in OUT.parents:
        raise SystemExit('output must stay inside %s' % STAGE)

    raw = BASE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASE_SHA256 or len(raw) != BASE_BYTES:
        raise SystemExit('base ADT does not match the pinned P23 hardware candidate')
    original = decode(raw)
    if emit(original) != raw:
        raise SystemExit('base ADT is not byte-exact under decode(encode()); refusing to edit')
    if 'text-fixups' in child(original, 'chosen')['properties']:
        raise SystemExit('base ADT still carries /chosen/text-fixups')

    tree = decode(raw)

    # --- root node: model / target identity ---
    props = tree['properties']
    if tree['name'] != 'device-tree':
        raise SystemExit('unexpected ADT root name %r' % tree['name'])
    if props['model'] != b'iPad7,1\0' or props['target-type'] != b'J120\0':
        raise SystemExit('base identity is not the expected P23 iPad7,1/J120')
    props['model'] = IDENTITY['model']
    props['target-type'] = IDENTITY['target-type']
    tree['properties'] = insert_after(props, 'target-type', 'target-sub-type',
                                      IDENTITY['target-sub-type'])

    # --- chosen: root-matching keeps the P23 XML-plist text encoding ---
    chosen = child(tree, 'chosen')
    old_matching = chosen['properties']['root-matching']
    if not old_matching.endswith(b'\0') or OLD_ROOT_UUID.encode() not in old_matching:
        raise SystemExit('base /chosen/root-matching is not the expected P23 encoding')
    new_matching = old_matching.replace(OLD_ROOT_UUID.encode(), ROOT_UUID.encode())
    if new_matching != (ROOT_MATCHING % ROOT_UUID).encode() or len(new_matching) != len(old_matching):
        raise SystemExit('root-matching rewrite changed the P23 plist form')
    chosen['properties']['root-matching'] = new_matching

    # --- product node ---
    product = child(tree, 'product')
    prod = product['properties']
    if prod['artwork-display-gamut'] != b'P3\0':
        raise SystemExit('base /product/artwork-display-gamut is not P3')
    for key in ('artwork-scale-factor', 'artwork-device-subtype'):
        if len(prod[key]) != 4:
            raise SystemExit('/product/%s is not a u32 in the base tree' % key)
    prod['product-name'] = IDENTITY['product-name']
    prod['product-description'] = IDENTITY['product-description']
    prod['artwork-device-idiom'] = IDENTITY['artwork-device-idiom']
    prod['compatible-device-fallback'] = IDENTITY['compatible-device-fallback']
    prod['artwork-scale-factor'] = u32(3)
    prod['artwork-device-subtype'] = u32(2436)
    if 'button-names' in prod:
        raise SystemExit('/product/button-names belongs under /buttons')

    # --- top-level /buttons: only the MSM8998-neutral declaration + names ---
    if any(c['name'] == 'buttons' for c in tree['children']):
        raise SystemExit('base ADT already has /buttons; inspect rather than append')
    tree['children'].append({
        'name': BUTTONS_NODE['name'],
        'properties': dict(BUTTONS_NODE['properties']),
        'children': [],
    })

    # --- encode and prove the result roundtrips ---
    result = emit(tree)
    rebuilt = decode(result)
    if rebuilt != tree:
        raise SystemExit('decode(encode(new tree)) != new tree')
    if emit(rebuilt) != result:
        raise SystemExit('new ADT is not byte-stable')

    # --- structured diff: exactly the intended properties, nothing else ---
    changes = diff(original, rebuilt, allow_new=('buttons',))
    paths = [path for path, _, _ in changes]
    if sorted(paths) != sorted(CHANGED) or len(paths) != len(CHANGED):
        raise SystemExit('unexpected property delta:\n  got %s\n  want %s'
                         % (json.dumps(sorted(paths), indent=2),
                            json.dumps(sorted(CHANGED), indent=2)))
    for path in paths:
        after = dict((p, a) for p, _, a in changes)[path]
        if after is None:
            raise SystemExit('%s was removed' % path)

    old_children = {c['name']: c for c in original['children']}
    new_children = {c['name']: c for c in rebuilt['children']}
    for name in HARDWARE_NODES:
        if old_children[name] != new_children[name]:
            raise SystemExit('hardware node /%s changed' % name)
    for name in ('memory-map', 'manifest-properties'):
        if child(old_children['chosen'], name) != child(new_children['chosen'], name):
            raise SystemExit('/chosen/%s changed' % name)
    if new_children['chosen']['children'] != old_children['chosen']['children']:
        raise SystemExit('/chosen children changed')
    if rebuilt['properties']['compatible'] != original['properties']['compatible']:
        raise SystemExit('root compatible changed; it is out of scope')
    if rebuilt['properties']['unixnu,board-id'] != b'oneplus,cheeseburger\0':
        raise SystemExit('board marker changed')
    if [c['name'] for c in rebuilt['children']] != \
            [c['name'] for c in original['children']] + ['buttons']:
        raise SystemExit('root child list must only gain a trailing /buttons')
    buttons = new_children['buttons']
    if buttons != BUTTONS_NODE:
        raise SystemExit('/buttons does not match the declared MSM8998-neutral node')

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with OUT.open('xb') as handle:
        handle.write(result)
    if hashlib.sha256(BASE.read_bytes()).hexdigest() != BASE_SHA256:
        raise SystemExit('base ADT changed during the build')

    report = {
        'result': 'STAGE_C_D22AP_ADT_IDENTITY_PASS',
        'sha256': hashlib.sha256(result).hexdigest(),
        'bytes': len(result),
        'base_sha256': BASE_SHA256,
        'base_bytes': BASE_BYTES,
        'base_unchanged_after_build': True,
        'root_matching': ROOT_UUID,
        'root_matching_encoding': 'P23 XML-plist text (OSUnserializeXML dict), uppercase UUID',
        'root_matching_uuid_big_endian_hex': uuid.UUID(ROOT_UUID).bytes.hex(),
        'identity': {
            'target-type': 'D22',
            'target-sub-type': 'D22AP',
            'model': 'iPhone10,3',
            'product-name': 'iPhone X',
            'product-description': 'iPhone X',
            'artwork-device-idiom': 'phone',
            'artwork-scale-factor': 3,
            'artwork-device-subtype': 2436,
            'compatible-device-fallback': 'iPhone9,2',
            'button-names': list(BUTTON_NAMES),
            'button-names-node': '/buttons',
            'button-names-raw-hex': names.hex(),
            'button-names-encoding': 'NUL-separated C strings (D22AP donor layout)',
            'artwork-display-gamut': 'P3',
        },
        'roundtrip': True,
        'hardware_nodes_unchanged': True,
        'hardware_nodes_kept': HARDWARE_NODES + ['chosen/memory-map', 'chosen/manifest-properties'],
        'added_nodes': ['/buttons'],
        'changed_properties': sorted(paths),
        'changed_property_details': [
            {'property': path,
             'before_hex': before.hex() if before is not None else None,
             'after_hex': after.hex() if after is not None else None}
            for path, before, after in sorted(changes)
        ],
        'hardware_verified': False,
        'phone_operations': [],
        'provenance': {
            'donor_dtb': 'analysis/root-trust-data/donor/DeviceTree.d22ap.dtb',
            'donor_dtb_sha256': DONOR_DTB_SHA256,
            'adt_identity_json': 'analysis/root-trust-data/adt-identity.json',
            'adt_identity_json_sha256': IDENTITY_JSON_SHA256,
        },
        'out_of_scope_notes': [
            'root compatible still device-tree/qcom,msm8998; the D22AP donor uses '
            'D22AP/iPhone10,3/AppleARM and is not changed here',
            '/buttons is a minimal copy of the donor node: only name/compatible/'
            'device_type/button-names; the donor AAPL,phandle, interrupts_pre_p2, '
            'interrupt-parent_pre_p2, function-button_*, button-priority_*, '
            'button-delay_*, home-button-type and opposed-power-vol-buttons describe '
            'Apple GPIO/PMU wiring, not MSM8998 hardware, and are deliberately omitted',
            '1125x2436 logical canvas is a NativeLegacyDisplay/board-runtime input, '
            'not an ADT property; unchanged here',
            'donor /chosen/root-matching is a 256-byte iBoot placeholder; this tree keeps '
            'the P23 XML-plist form consumed by NativeUfs.cpp',
        ],
    }
    with REPORT.open('x') as handle:
        handle.write(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in
                      ('result', 'sha256', 'bytes', 'added_nodes', 'changed_properties',
                       'roundtrip', 'hardware_nodes_unchanged', 'hardware_verified')}, indent=2))


if __name__ == '__main__':
    main()
