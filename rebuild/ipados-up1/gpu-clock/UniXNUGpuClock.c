/** @file
  Read-only GPU clock and power-domain inventory, through the firmware
  ClockDxe EFI_CLOCK_PROTOCOL.

  This does not enable anything. It asks the firmware clock driver for the
  IDs and state of the A540 clocks and for the GPU power-domain IDs, so the
  next step can power them in the official order instead of guessing register
  values. The result is one ASCII line written to the spare block at the end
  of the diagnostic log window (RailWriteCapture) and printed to the console.

  Names are the ones present in this device's ClockDxe:
    VDD_GPU_CX / VDD_GPU_GX / VDD_HLOS1_GPU_SMMU / VDD_HLOS2_GPU_SMMU
    gcc_gpu_cfg_ahb_clk, gcc_gpu_bimc_gfx_clk, gcc_gpu_snoc_dvm_gfx_clk,
    gpucc_cxo_clk, gpucc_rbcpr_clk, gpucc_rbbmtimer_clk,
    gpucc_gfx3d_isense_clk, gpucc_gfx3d_clk

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "EFIClock.h"

// Opt-in power experiment. 0 keeps the image read-only; set to 1 only with the
// 400 s kernel auto-reset image as the safety net.
#define UNIXNU_GPU_POWER 1

EFI_GUID gEfiClockProtocolGuid = EFI_CLOCK_PROTOCOL_GUID;

extern EFI_STATUS RailWriteCapture (CONST CHAR8 *Line, UINTN Len);

#define GPU_ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

STATIC CONST CHAR8 *mGpuClocks[] = {
  "gcc_gpu_cfg_ahb_clk",
  "gcc_gpu_bimc_gfx_clk",
  "gcc_gpu_snoc_dvm_gfx_clk",
  "gpucc_cxo_clk",
  "gpucc_rbcpr_clk",
  "gpucc_rbbmtimer_clk",
  "gpucc_gfx3d_isense_clk",
  "gpucc_gfx3d_clk"
};

STATIC CONST CHAR8 *mGpuDomains[] = {
  "VDD_GPU_CX",
  "VDD_GPU_GX",
  "VDD_HLOS1_GPU_SMMU",
  "VDD_HLOS2_GPU_SMMU"
};

VOID
UniXnuGpuClockProbe (
  VOID
  )
{
  EFI_CLOCK_PROTOCOL *Clock;
  EFI_STATUS          Status;
  CHAR8               Line[4096];
  UINTN               Len;
  UINTN               Written;

  Clock  = NULL;
  Len    = 0;
  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  Written = AsciiSPrint (Line, sizeof (Line), "UP1GPU1 clock=%x", Status);
  if (Written != 0) {
    Len = Written;
  }

  if (!EFI_ERROR (Status) && (Clock != NULL)) {
    Written = AsciiSPrint (Line + Len, sizeof (Line) - Len, " ver=%lx", (UINTN)Clock->Version);
    if (Written != 0) {
      Len += Written;
    }

    for (UINTN Index = 0; Index < GPU_ARRAY_SIZE (mGpuClocks); ++Index) {
      UINTN      Id;
      BOOLEAN    Enabled;
      BOOLEAN    On;
      UINT32     Freq;
      EFI_STATUS Get;
      EFI_STATUS EnabledStatus;
      EFI_STATUS OnStatus;
      EFI_STATUS FreqStatus;

      Id            = 0;
      Enabled       = FALSE;
      On            = FALSE;
      Freq          = 0;
      EnabledStatus = EFI_DEVICE_ERROR;
      OnStatus      = EFI_DEVICE_ERROR;
      FreqStatus    = EFI_DEVICE_ERROR;
      Get           = Clock->GetClockID (Clock, mGpuClocks[Index], &Id);
      if (!EFI_ERROR (Get)) {
        EnabledStatus = Clock->IsClockEnabled (Clock, Id, &Enabled);
        OnStatus      = Clock->IsClockOn (Clock, Id, &On);
        FreqStatus    = Clock->GetClockFreqHz (Clock, Id, &Freq);
      }
      Written = AsciiSPrint (
                  Line + Len, sizeof (Line) - Len,
                  " | %a=%lx,%x,%x,%x,%x,%x,%x,%u",
                  mGpuClocks[Index], (UINTN)Id, Get, EnabledStatus, (UINTN)Enabled,
                  OnStatus, (UINTN)On, FreqStatus, Freq);
      if (Written == 0) {
        break;
      }
      Len += Written;
    }

    for (UINTN Index = 0; Index < GPU_ARRAY_SIZE (mGpuDomains); ++Index) {
      UINTN      Id;
      EFI_STATUS Get;

      Id  = 0;
      Get = Clock->GetClockPowerDomainID (Clock, mGpuDomains[Index], &Id);
      Written = AsciiSPrint (
                  Line + Len, sizeof (Line) - Len,
                  " | %a=%lx,%x", mGpuDomains[Index], (UINTN)Id, Get);
      if (Written == 0) {
        break;
      }
      Len += Written;
    }
  }

  RailWriteCapture (Line, Len);
  Print (L"UniXNU gpu-clock: %a\n", Line);
}

#if UNIXNU_GPU_POWER
typedef struct {
  EFI_CLOCK_PROTOCOL *Clock;
  CHAR8               Line[2048];
  UINTN               Len;
} GPU_POWER_LOG;

STATIC VOID
PowerFlush (
  GPU_POWER_LOG *Log
  )
{
  RailWriteCapture (Log->Line, Log->Len);
}

STATIC VOID
PowerAdd (
  GPU_POWER_LOG *Log,
  UINTN          Written
  )
{
  if (Written != 0) {
    Log->Len += Written;
  }
  PowerFlush (Log);
}

STATIC EFI_STATUS
PowerDomain (
  GPU_POWER_LOG *Log,
  CONST CHAR8   *Name
  )
{
  UINTN      Id;
  EFI_STATUS Get;
  EFI_STATUS Enable;

  Id     = 0;
  Get    = Log->Clock->GetClockPowerDomainID (Log->Clock, Name, &Id);
  Enable = EFI_ERROR (Get) ? Get : Log->Clock->EnableClockPowerDomain (Log->Clock, Id);
  PowerAdd (Log, AsciiSPrint (
                   Log->Line + Log->Len, sizeof (Log->Line) - Log->Len,
                   " | D:%a=%lx,%x,%x", Name, (UINTN)Id, Get, Enable));
  return Enable;
}

STATIC EFI_STATUS
PowerClock (
  GPU_POWER_LOG *Log,
  CONST CHAR8   *Name
  )
{
  UINTN      Id;
  BOOLEAN    On;
  EFI_STATUS Get;
  EFI_STATUS Enable;
  EFI_STATUS OnStatus;

  Id       = 0;
  On       = FALSE;
  Enable   = EFI_DEVICE_ERROR;
  OnStatus = EFI_DEVICE_ERROR;
  Get      = Log->Clock->GetClockID (Log->Clock, Name, &Id);
  if (!EFI_ERROR (Get)) {
    Enable   = Log->Clock->EnableClock (Log->Clock, Id);
    OnStatus = Log->Clock->IsClockOn (Log->Clock, Id, &On);
  }
  PowerAdd (Log, AsciiSPrint (
                   Log->Line + Log->Len, sizeof (Log->Line) - Log->Len,
                   " | C:%a=%lx,%x,%x,%x,%x", Name, (UINTN)Id, Get, Enable, OnStatus, (UINTN)On));
  return Enable;
}

STATIC EFI_STATUS
PowerRate (
  GPU_POWER_LOG *Log,
  CONST CHAR8   *Name,
  UINT32         Hertz
  )
{
  UINTN      Id;
  UINT32     Result;
  EFI_STATUS Get;
  EFI_STATUS Set;

  Id     = 0;
  Result = 0;
  Get    = Log->Clock->GetClockID (Log->Clock, Name, &Id);
  Set    = EFI_ERROR (Get) ? Get :
           Log->Clock->SetClockFreqHz (Log->Clock, Id, Hertz, EFI_CLOCK_FREQUENCY_HZ_EXACT, &Result);
  PowerAdd (Log, AsciiSPrint (
                   Log->Line + Log->Len, sizeof (Log->Line) - Log->Len,
                   " | R:%a=%lx,%x,%x,%u", Name, (UINTN)Id, Get, Set, Result));
  return Set;
}

// Kept for the next step (a gated read after the bus/NoC path is qualified);
// the first UEFI read after power-up stopped the boot, so this build does not
// call it.
STATIC VOID __attribute__((unused))
PowerReadGpu (
  GPU_POWER_LOG *Log
  )
{
  UINT32 Cx;
  UINT32 Gx;
  UINT32 Gfx;

  Cx  = MmioRead32 (0x05065008);
  Gx  = MmioRead32 (0x05065094);
  Gfx = MmioRead32 (0x05065098);
  PowerAdd (Log, AsciiSPrint (
                   Log->Line + Log->Len, sizeof (Log->Line) - Log->Len,
                   " | G:%x,%x,%x", Cx, Gx, Gfx));
  if (((Gx & 0x80000000U) == 0) || ((Gfx & 1U) == 0) || ((Gfx & 0x80000000U) != 0)) {
    return;
  }
  PowerAdd (Log, AsciiSPrint (
                   Log->Line + Log->Len, sizeof (Log->Line) - Log->Len,
                   " | GPU:%x,%x,%x,%x,%x,%x",
                   MmioRead32 (0x05000000), MmioRead32 (0x05000004),
                   MmioRead32 (0x050013d4), MmioRead32 (0x050014c0),
                   MmioRead32 (0x0502a380), MmioRead32 (0x0502a384)));
}
#endif

// Opt-in step 2: power the GPU through the firmware clock driver. The sequence
// follows the sagit A540 bring-up: CX, GX, XO-domain clocks, gfx3d rate,
// gfx3d branch, then the GPU SMMU domains. Every completed step is written to
// the capture block before the next call, so a hang leaves the last step.
VOID
UniXnuGpuPowerEnable (
  VOID
  )
{
#if UNIXNU_GPU_POWER
  EFI_CLOCK_PROTOCOL *Clock;
  EFI_STATUS          Status;
  GPU_POWER_LOG       Log;

  Clock  = NULL;
  Log.Clock = NULL;
  Log.Len   = 0;
  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status) || (Clock == NULL)) {
    PowerAdd (&Log, AsciiSPrint (Log.Line, sizeof (Log.Line), "UP1GPU2 clock=%x", Status));
    return;
  }
  Log.Clock = Clock;
  PowerAdd (&Log, AsciiSPrint (Log.Line, sizeof (Log.Line), "UP1GPU2 ver=%lx", (UINTN)Clock->Version));

  if (EFI_ERROR (PowerDomain (&Log, "VDD_GPU_CX"))) {
    return;
  }
  if (EFI_ERROR (PowerDomain (&Log, "VDD_GPU_GX"))) {
    return;
  }
  PowerClock (&Log, "gpucc_cxo_clk");
  PowerClock (&Log, "gpucc_rbcpr_clk");
  PowerClock (&Log, "gpucc_rbbmtimer_clk");
  PowerClock (&Log, "gpucc_gfx3d_isense_clk");
  if (EFI_ERROR (PowerRate (&Log, "gpucc_gfx3d_clk", 257000000))) {
    return;
  }
  if (EFI_ERROR (PowerClock (&Log, "gpucc_gfx3d_clk"))) {
    return;
  }
  PowerDomain (&Log, "VDD_HLOS1_GPU_SMMU");
  PowerDomain (&Log, "VDD_HLOS2_GPU_SMMU");
  // Do not read GPU/GPUCC/SMMU MMIO here: after the clock driver powers the
  // domains, the first UEFI read stopped the boot (black screen, no kernel).
  // Report completion and let a later, explicitly gated probe read registers.
  PowerAdd (&Log, AsciiSPrint (Log.Line + Log.Len, sizeof (Log.Line) - Log.Len, " | P:done"));
#endif
}
