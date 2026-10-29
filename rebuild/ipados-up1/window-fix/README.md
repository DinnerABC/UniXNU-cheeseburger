# OnePlus 5 upper RAM window

Reuse the P23 Stage C local DarwinBoot range patch and `WindowRange.h`; this is a device adaptation, not a claim that the fixed official upstream has this feature. The captured OnePlus 5 FDT records RAM at `0x100000000 + 0xbd8c0000`. The ADT selects that range and requests 2 GiB. Set `PcdSystemMemorySize` from `0xf0000000` to `0x13d8c0000` in `Platforms/Msm8998Pkg/Msm8998.dsc`, matching the prior P23 firmware. The EFI allocator still requires a conventional-memory run and successful `AllocatePages`; the range does not free reserved pages.

`darwinboot-window-range.patch` includes an additional bounds and kernel/physical block-offset alignment check on the P23 AllocateAnyPages fallback. Apply from `DarwinBootPkg/Driver` with `patch -p1`, then place `WindowRange.h` there. The build retains the verified iPadOS kernel, touch and 8-CPU settings. `window-test.c` checks range, alignment, insufficient capacity and overflow on the host; it does not establish a real firmware allocation or RAM mapping.

Reference: `docs/P4_ADT_BRINGUP.md`, local P23 `out/p23/ios-16.7.16-20H392/adt/kernel.macho.adt` and Stage C firmware overlay. Preserve the original pinned upstream source and all earlier candidate artifacts.
