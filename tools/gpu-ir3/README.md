# A540 IR3 shader assembly

`scan_band.asm` is the compute shader embedded in
`package/BoardRuntime/CheeseburgerGpuCompute540.h`. It writes one dword per
invocation into SSBO 0 at `id << 2`, which the kernel binds to the firmware
scanout band; the byte-addressed store is what gets around the GOP's
64-byte 2D/3D pitch rule (the real pitch here is 4320 bytes).

Assemble with Mesa 26.2's IR3 assembler (the same parser computerator uses):

1. Build `ir3_parse_asm` from a configured Mesa tree:
   - sources: `src/freedreno/ir3/ir3_assembler.c` plus `libfreedreno_ir3`
   - headers: `include/`, `src/`, `src/freedreno{,/ir3,/common}`,
     `src/compiler{,/nir}`, `src/util`, the build tree, and the generated
     `builtin_types.h` / `ir3_parser.h`
   - the local command used for this round is recorded in the round log
     (`/tmp/ir3work/ir3asm`); it creates the compiler with
     `fd_dev_info_raw(gpu_id=540)` and `disable_cache = true`.
2. Run it on the shader. The result for `scan_band.asm` is:

```
instrlen=1 constlen=0 maxreg=1 double_threadsize=1 localid=0 wgid=252
20080000 46d00004 00000000 00000400 00800001 c7660004 00000000 03000000
```

The 32-word unit is copied verbatim into `ScanBand540::Code`. The metadata in
the header (`InstrLen`, `FullRegs = max_reg + 1`, `ThreadSize`,
`LocalIdReg`, `WgIdConst`) comes from the same output and from sagit's
`AdrenoPipeline.h` compute state.
