# P21-R4 GUI then black: upstream comparison

2026-10-25. Read-only research, no candidate rebuilt and no device transition.

## Current device evidence

Original photograph: six validation calls, zero rejected, six transfers, four scaled copies, stride 5120; user reports visible `Press home to upgrade` then black. UI-role fatal-exit counters zero. This does not establish the last frame's contents, pending queue, power request, HID delivery or a continuing frame stall.

## Refreshed official revisions

- NativeMobileDisplay main: cf3b42979a9c44219e7bb149543e6fc3388b58ed.
- UniXNU-sheng main tree: 0e1f6e52638137db15905db8d775ad73757f39fa.
- Pagani main tree: fcab49e2ea2f9ddfaa08ee6e8109ba7ed67029ac.
- ios-topaz topaz tree: e4772911e116ab4ab6c4ac1f38e6106fefe8b483.

Fetched files and patches retained under out/p21/r4/audit/upstream-black-screen.

## 1. Official display queue, vsync and console interference

https://github.com/UniXNU/NativeMobileDisplay/blob/cf3b42979a9c44219e7bb149543e6fc3388b58ed/NativeLegacyDisplay.cpp

Source describes int-143: queued frame waited for another submission; still screen showed the previous frame until another touch. Fix is re-kick after retirement and a reentrancy loop. R4 already contains these methods. Source also describes int-093 console repaint over GPU scanout; R4 retains copy-owner exclusion and observer restoration, not a permanent diagnostic mute. Differential-copy experiment was associated with an unresponsive lock screen, so upstream defaults it off; R4 also leaves it off. These are concrete historical mechanisms, not proof of this device's current cause.

R4 includes a software vsync timer. Configuration requests 60 Hz. Timer existence, observer configuration and delivery to actual clients are distinct; the current page does not expose armed/sent/idle/late counts or client-list state. The upstream native_legacy_prepare_report already exports vsync counters and can be reused for compact diagnostics.

## 2. Official HID Home test: useful next integration

https://github.com/UniXNU/UniXNU-sheng/blob/0e1f6e52638137db15905db8d775ad73757f39fa/package/HidInputProbe.cpp

Probe loads IOHIDFamily/IOHIDEventDriver and checks runtime metaclass sizes before allocating its owned IOHIDDevice. Gates include offline-hid-input=1, offline-hid-home=1. `offline-hid-auto-home=<seconds>` starts after at least two transfer observations; count defaults to one and is bounded to eight, interval at least five seconds. The input source must also be selected, compiled, linked and initialized; flags alone are insufficient.

Official late probe calls native_hid_input_probe and sheng_buttons_probe. R4 has declarations but omits these board peripheral call sites; llvm-nm of R4 finds no sheng_buttons_probe symbol. Thus no tested physical Home route is established by this candidate. This is a source finding, not proof of the black-screen cause.

https://github.com/UniXNU/UniXNU-sheng/blob/0e1f6e52638137db15905db8d775ad73757f39fa/package/ButtonProbe.cpp

The physical driver is PMK8550/PM8550 SPMI/PDC-specific and describes power/volume, not this OnePlus5 capacitive Home device. It must not be enabled on MSM8998 using sheng register addresses. Reuse the qualified generic HID path first; physical input requires OnePlus5 provenance and separate adaptation.

## 3. Community sweet/topaz case

https://github.com/MakrSas/ios-topaz/blob/e4772911e116ab4ab6c4ac1f38e6106fefe8b483/NOTES.md

Maintainer records Hello with no input, then 0058 HID services, a 0059 test Home press advancing Hello to the language list, and 0060 physical input. Power-off left the kernel console visible because panel power was not actually switched. Similar symptom and useful isolation method, but iOS16/another hardware platform, not an interchangeable ABI or direct fix for iPadOS17.

0056 throttle patch and notes record 60 Hz full-frame copying starving a single CPU during migration; deferred latest-frame copy reduces pressure. The actual patch was read. Current OnePlus5 evidence has six transfers total, so no evidence yet for that high-frequency-copy mechanism. Its latest-frame record/ownership model also differs from the original Legacy request queue. Do not import blindly or claim that setting a lower vsync rate cures this black screen.

## 4. Same OS community reference

https://github.com/IssacL04/UniXNU-Pagani_SM8750/blob/fcab49e2ea2f9ddfaa08ee6e8109ba7ed67029ac/README.md

Maintainer distinguishes observed build39 iPadOS17.7.11 CPU framebuffer UI from newer input/backlight additions that passed host checks but were not boot-tested. Useful architecture/reference, not proof of a working black-screen fix on OnePlus5.

## Recommended next candidate

1. Preserve R4 scaler and R10 hardware baseline.
2. Integrate official generic HID source with original target-version ABI verification and one bounded Home event after genuine successful presentation; retain default-off controls and record driver match/report results.
3. Show last transfer age/type, explicit blank-fill count, last frame black/nonblack summary, pending/completed generation, actual vsync sent/idle/late, logical power ordinal and HID event results on the existing single page.
4. Do not alter physical PMIC registers, model identity, migration completion flags or persistent storage based on symptom similarity.

No exact published patch was found proving a cure for this device's `Press home to upgrade -> black` sequence. The strongest newly identified missing integration is qualified HID input; causality still requires the bounded test and display-state diagnostics.
