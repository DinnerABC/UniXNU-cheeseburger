#!/usr/bin/env python3
"""Stage source-only board adapters with provenance; never copy old objects."""
from pathlib import Path
import hashlib,json,shutil,difflib
R=Path(__file__).resolve().parents[1];U=R/'.work/p21-official-framework';D=R/'package/BoardRuntime';D.mkdir(parents=True,exist_ok=True);rows=[]
def copy(src,name=None):
 src=R/src;dst=D/(name or src.name);shutil.copy2(src,dst);rows.append({'output':str(dst.relative_to(R)),'source':str(src.relative_to(R)),'sha256':hashlib.sha256(src.read_bytes()).hexdigest()});return dst
for name in ['NativeLegacyDisplay.cpp','NativeLegacyRequest.h','P21SwapDebug.h','LegacyDisplayProbe.cpp','HidInputProbe.cpp','CheeseburgerButtons.cpp','CheeseburgerKeys.h','KeyStoreProbe.cpp','KeyStoreFixture.cpp','CompactObserver.c','DisplayObservation.c','BootProcessProbe.c','P20ExitProbe.c','P20FaultExit.h','P20UiFault.h','BootFaultProbe.c','StartupWaitProbe.c','BootUiTraceCore.h']:
 copy(Path('out/p21/r7/source')/name)
copy(Path('.work/p21-official-framework/drivers/AppleKeyStoreState/NativeKeyStoreEvents.cpp'))
hid=copy(Path('out/p21/r7/audit/official-workflow/component-sources/ShengHidInput/NativeHidInput.cpp'))
assert hashlib.sha256(hid.read_bytes()).hexdigest()=='0445e5f451ffcc827bdd603e822180444d998427fa3a6d63559b1a7b8f850998'
# Compile out the optional touchscreen producer; there is no qualified touch sensor
# on this board. Do not satisfy this contract with an unpinned QEMU TouchState.o.
old=hid.read_text();s=old.replace('#include "OriginalHidDeviceABI.h"','#ifndef UNIXNU_HID_TOUCH_SOURCE\n#define UNIXNU_HID_TOUCH_SOURCE 1\n#endif\n#include "OriginalHidDeviceABI.h"')
start=s.index('    void touchSend() {')+len('    void touchSend() {');end=s.index('\n    // The button driver',start);body=s[start:end];assert body.rstrip().endswith('}')
pos=body.rfind('}');body='\n#if UNIXNU_HID_TOUCH_SOURCE'+body[:pos]+'\n#endif\n    }\n';s=s[:start]+body+s[end:]
s=s.replace('if(&native_touch_set_sink)native_touch_set_sink(&NativeHidInput::touchNotify,this);','#if UNIXNU_HID_TOUCH_SOURCE\n            if(&native_touch_set_sink)native_touch_set_sink(&NativeHidInput::touchNotify,this);\n#endif')
s=s.replace('        if((touchOnly||touchEnabled) && &native_touch_set_sink)native_touch_set_sink(nullptr,nullptr);','#if UNIXNU_HID_TOUCH_SOURCE\n        if((touchOnly||touchEnabled) && &native_touch_set_sink)native_touch_set_sink(nullptr,nullptr);\n#endif')
s=s.replace('        inputLoop=IOWorkLoop::workLoop();if(!inputLoop)return false;','''#if !UNIXNU_HID_TOUCH_SOURCE
        unsigned unsupportedTouch=0;
        if(touchOnly || (PE_parse_boot_argn("sheng-touch-hid",&unsupportedTouch,sizeof(unsupportedTouch)) && unsupportedTouch))return false;
#endif
        inputLoop=IOWorkLoop::workLoop();if(!inputLoop)return false;''')
hid.write_text(s);p=R/'patches/components';p.mkdir(exist_ok=True);(p/'hid-optional-touch-producer.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/NativeHidInput.cpp',tofile='b/NativeHidInput.cpp')))
for f in ['NativeGOPPresenter.cpp','NativeGOPWire.h']:copy(Path('.work/p21-official-framework/drivers/GopFramebuffer')/f)
copy(Path('.work/p20-r11/framework/build/native-legacy-display/OriginalLegacyFramebufferABI.h'))
copy(Path('.work/p20-r11/framework/build/native-legacy-display/original-bindings.S'),'display-original-bindings.S')
for f in ['OriginalHidDeviceABI.h','original-bindings.S']:copy(Path('out/p21/r7/hid')/f,'hid-original-bindings.S' if f.endswith('.S') else None)
copy(Path('drivers/p12/NativeUfs/NativeUfs.cpp'))
for p in (R/'drivers/p12/NativeUfs').glob('*.h'):copy(p.relative_to(R))
for p in (R/'drivers/p11/NativeUfs').glob('*.h'):
 if not (D/p.name).exists():copy(p.relative_to(R))
copy(Path('drivers/CheeseburgerPlatformPower/CheeseburgerPlatformPower.cpp'))
for name in ['OriginalPMGRABI.h','GuardedPMGRSlots.inc','original-bindings.S']:
 copy(Path('out/p21/official-adaptation/pmgr')/name,'pmgr-original-bindings.S' if name.endswith('.S') else None)
copy(Path('out/p12/native-ufs/original-bindings.S'),'ufs-original-bindings.S')
copy(Path('out/p17/r3/data-driver/LateRegistration.cpp'),'RamLateRegistration.cpp')
copy(Path('out/p17/r3/source/data-driver/MediaInventory.cpp'))
copy(Path('out/p17/r3/source/data-driver/DataActivation.cpp'))
copy(Path('out/p17/source/data-driver/no-store-provider.c'))
copy(Path('out/p16/data-driver/original-bindings.S'),'ram-original-bindings.S')
copy(Path('drivers/p14/RootAuth/RootAuth.cpp'));copy(Path('drivers/p14/RootAuth/RootDecision.h'))
copy(Path('out/p15/source/CacheAuth.cpp'))
copy(Path('out/p19/r2/source/BootWatchdogPolicy.c'))
copy(Path('.work/p21-official-framework/drivers/RamBlock/NativeRamBlock.cpp'))
for p in (U/'drivers/RamBlock').glob('*.h'):copy(p.relative_to(R))
copy(Path('out/p12/policies/VerifiedBlockPolicy.cpp')) if (R/'out/p12/policies/VerifiedBlockPolicy.cpp').exists() else None
(O:=R/'out/p21/official-adaptation')
(O/'board-source-provenance.json').write_text(json.dumps({'source_files':rows,'objects_copied':0,'hid_source_commit':'5f3a37cec0c3fc4234b10726b25489a55f0d77ef','hid_optional_touch_compilation':False,'note':'Board adaptations and ABI evidence are retained as source; all final objects must be rebuilt with the new kernel compile records.'},indent=2)+'\n')
print('Staged source-only board files:',len(rows))
