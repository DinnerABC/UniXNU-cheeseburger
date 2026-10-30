/** @file
  Enable the two PM8998 LDOs that feed the OnePlus 5 S3320 touch controller
  before the UniXNU payload starts, through the SPMI PMIC arbiter.

  Board facts, all from this device:
    - Captured FDT: pm8998_l6 = ldoa id 6, 1.808 V, pm8998_l28 = ldoa id 0x1c, 3.008 V,
      both qcom,rpm-smd regulators with qcom,set = <3>; the captured DTS gives the
      arbiter as core 0x0800f000, chnls 0x08400000, obsrvr 0x09400000, EE 0.
    - Recovery dmesg: the stock S3320 driver probes address 0x20 at 2.4 s, so the
      sensor itself, its address and its reset wiring are known good.
    - XNU touch probe with the reset released gets an SPMI-free I2C NACK
      (QUP status bit 8), which is what an unpowered sensor looks like.
    - Upstream sheng firmware reads PMIC registers through the same arbiter
      (Platforms/Xiaomi/shengPkg/Drivers/ShengKeyInputDxe/ShengSpmiKeys.h):
      APID found by PPID in the core map, register read on the observer channel
      with opcode 1, address bits 4..11, data at +0x18.
    - Linux spmi-pmic-arb.c arbiter v2 command format:
      (opcode << 27) | ((addr & 0xff) << 4) | (byte_count & 7);
      channel layout cmd 0x00, status 0x08, wdata0 0x10, rdata0 0x18,
      channel = 0x8000 * apid + 0x1000 * ee.
    - PM8998 LDO (PMIC4) register page 0x4000 + 0x100 * id: voltage control at
      +0x40, enable control at +0x46 (bit 7 = enabled).

  Only the enable bit of a previously programmed LDO is touched: the voltage
  register is read first and a zero VSET is left alone rather than guessed.
  Every read and write is printed so a failed boot still shows the register
  state it saw.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/BlockIo.h>

#define RAIL_CORE_BASE      0x0800F000u
#define RAIL_CHNL_BASE      0x08400000u
#define RAIL_OBS_BASE       0x09400000u

#define RAIL_APID_MAP_OFF   0x0800u
#define RAIL_APID_STRIDE    0x8000u
#define RAIL_EE_STRIDE      0x1000u

#define RAIL_CH_CMD         0x00u
#define RAIL_CH_STATUS      0x08u
#define RAIL_CH_WDATA0      0x10u
#define RAIL_CH_RDATA0      0x18u

#define RAIL_OP_WRITE       0u
#define RAIL_OP_READ        1u

#define RAIL_STATUS_DONE    (1u << 0)
#define RAIL_STATUS_ERRORS  0x0eu
#define RAIL_POLLS          100000u

#define RAIL_REG_VSET       0x40u
#define RAIL_REG_ENABLE     0x46u
#define RAIL_ENABLE_BIT     0x80u

#define RAIL_PPID_LOA6      0x045u
#define RAIL_PPID_L28       0x05Bu

// Protocol GUIDs recovered offline from the firmware images and their depex
// expressions (rebuild/ipados-up1/npa-audit): PmicDxe locates AE37B942 and names
// it "NpaProtocol"; NpaDxe installs 79D6C870 and 276732AB. Only existence is
// probed here; no interface is called yet.
STATIC CONST EFI_GUID gNpaGuid  = {0xAE37B942,0x457F,0x4C91,{0xA1,0x96,0xD9,0x66,0x9F,0xD3,0x47,0xA3}};
STATIC CONST EFI_GUID gNpaAlt1 = {0x79D6C870,0x725E,0x489E,{0xA0,0xA1,0x27,0xE7,0xA5,0xD0,0xCB,0x35}};
STATIC CONST EFI_GUID gNpaAlt2 = {0x276732AB,0xBB39,0x4DCE,{0xB5,0xD9,0x27,0x46,0xCD,0xD6,0xD5,0x18}};

// EFI_DISPLAY_POWER_PROTOCOL from the firmware headers
// (Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIDisplayPwr.h): the struct is a
// revision followed by two function pointers. The panel power sequence is the
// only documented path that also votes the board's disp_prim_touch DAL client.
typedef struct {
  UINT32      Revision;
  EFI_STATUS (*SetDisplayPowerState) (VOID *This, UINT32 State);
  EFI_STATUS (*GetDisplayPowerState) (VOID *This, UINT32 *State);
} RAIL_DISPLAY_POWER;

STATIC CONST EFI_GUID gDisplayPwrGuid = {0xF352021D,0x9593,0x4432,{0xBF,0x04,0x67,0xB9,0xF3,0xB7,0x60,0x08}};

// NPA protocol table as used by UsbConfigDxe (same GUID 79D6C870): the first
// qword is the revision, +0x10 creates a client (resource, client name, type,
// param, extra, &handle) and +0x28 issues a request (handle, state).
typedef struct {
  UINT64     Revision;
  VOID      *Reserved08;
  EFI_STATUS (*CreateClient) (const char *Resource, const char *ClientName, UINT32 Type, UINT32 Param, VOID *Extra, VOID **Handle);
  VOID      *Reserved18;
  VOID      *Reserved20;
  EFI_STATUS (*Request) (VOID *Handle, UINT32 State);
} RAIL_NPA;

STATIC
INT32
RailFindApid (
  IN UINT32  PpPid,
  OUT UINT32 *Apid
  )
{
  UINT32 Version = MmioRead32 (RAIL_CORE_BASE);
  UINT32 Features;
  UINT32 Count;
  UINT32 Index;

  *Apid = 0xFFFFFFFFu;
  if (Version < 0x20010000u || Version >= 0x50000000u) {
    return 1;
  }
  Features = MmioRead32 (RAIL_CORE_BASE + 0x04u);
  Count    = Features & 0x7ffu;
  if (Count == 0 || Count > 1024u) {
    return 2;
  }
  for (Index = 0; Index < Count; ++Index) {
    UINT32 Entry = MmioRead32 (RAIL_CORE_BASE + RAIL_APID_MAP_OFF + 4u * Index);
    if (Entry != 0 && ((Entry >> 8) & 0xfffu) == PpPid) {
      *Apid = Index;
      return 0;
    }
  }
  return 3;
}

STATIC
INT32
RailRead8 (
  IN  UINT32  Apid,
  IN  UINT8   Reg,
  OUT UINT8  *Value
  )
{
  UINT32 Channel = RAIL_OBS_BASE + RAIL_APID_STRIDE * Apid;
  UINT32 Polls;

  MmioWrite32 (Channel + RAIL_CH_CMD, (RAIL_OP_READ << 27) | ((UINT32)Reg << 4));
  for (Polls = 0; Polls < RAIL_POLLS; ++Polls) {
    UINT32 Status = MmioRead32 (Channel + RAIL_CH_STATUS);
    if ((Status & RAIL_STATUS_DONE) != 0) {
      if ((Status & RAIL_STATUS_ERRORS) != 0) {
        return 2;
      }
      *Value = (UINT8)MmioRead32 (Channel + RAIL_CH_RDATA0);
      return 0;
    }
  }
  return 1;
}

STATIC
INT32
RailWrite8 (
  IN UINT32  Apid,
  IN UINT8   Reg,
  IN UINT8   Value
  )
{
  UINT32 Channel = RAIL_CHNL_BASE + RAIL_APID_STRIDE * Apid;
  UINT32 Polls;

  MmioWrite32 (Channel + RAIL_CH_WDATA0, Value);
  MmioWrite32 (Channel + RAIL_CH_CMD, (RAIL_OP_WRITE << 27) | ((UINT32)Reg << 4));
  for (Polls = 0; Polls < RAIL_POLLS; ++Polls) {
    UINT32 Status = MmioRead32 (Channel + RAIL_CH_STATUS);
    if ((Status & RAIL_STATUS_DONE) != 0) {
      return ((Status & RAIL_STATUS_ERRORS) != 0) ? 2 : 0;
    }
  }
  return 1;
}

STATIC
VOID
RailEnable (
  IN CONST CHAR16 *Name,
  IN UINT32        PpPid
  )
{
  UINT32 Apid;
  UINT8  Vset = 0;
  UINT8  Enable = 0;
  UINT8  After = 0;
  INT32  Rc;
  INT32  WriteRc = -1;

  Rc = RailFindApid (PpPid, &Apid);
  if (Rc != 0) {
    Print (L"UniXNU rails: %s ppid=%x no apid rc=%d\n", Name, PpPid, Rc);
    return;
  }
  Rc = RailRead8 (Apid, RAIL_REG_VSET, &Vset);
  if (Rc != 0) {
    Print (L"UniXNU rails: %s apid=%d vset read rc=%d\n", Name, Apid, Rc);
    return;
  }
  Rc = RailRead8 (Apid, RAIL_REG_ENABLE, &Enable);
  if (Rc != 0) {
    Print (L"UniXNU rails: %s apid=%d vset=%02x enable read rc=%d\n", Name, Apid, Vset, Rc);
    return;
  }
  // A zero voltage control means nobody programmed this LDO: enable it would
  // apply an unknown voltage, so leave it alone and report it.
  if (Vset != 0 && (Enable & RAIL_ENABLE_BIT) == 0) {
    WriteRc = RailWrite8 (Apid, RAIL_REG_ENABLE, (UINT8)(Enable | RAIL_ENABLE_BIT));
  }
  if (RailRead8 (Apid, RAIL_REG_ENABLE, &After) != 0) {
    After = 0xff;
  }
  Print (
    L"UniXNU rails: %s apid=%d vset=%02x en=%02x write=%d en_after=%02x\n",
    Name,
    Apid,
    Vset,
    Enable,
    WriteRc,
    After
    );
}

// The probe result has to survive without anyone reading the screen, so it is
// written into the persistent log window the kernel owns. The kernel uses
// blocks 0..8160 of that window (header, message area and 247 slots of 32
// blocks); blocks 8161..8191 are never written by it. userdata starts at LUN0
// LBA 2441472 and the window at userdata LBA 2621440 (10 GiB), so the spare
// block is LUN0 LBA 5071073 or userdata LBA 2629601.
#define LOG_WINDOW_BLOCK        5071073ULL
#define LOG_WINDOW_PART_BLOCK   2629601ULL
#define LOG_USERDATA_BLOCKS     11703808ULL
#define LOG_LUN0_BLOCKS         14145536ULL

STATIC
EFI_STATUS
RailWriteCapture (
  IN CONST CHAR8  *Line,
  IN UINTN         Len
  )
{
  EFI_HANDLE            *Handles;
  UINTN                  Count;
  UINTN                  Index;
  EFI_BLOCK_IO_PROTOCOL *Bio;
  UINT64                 Lba;
  UINT8                 *Block;
  EFI_STATUS             Status;

  Handles = NULL;
  Count = 0;
  Bio = NULL;
  Lba = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  for (Index = 0; Index < Count; Index++) {
    EFI_BLOCK_IO_PROTOCOL *Candidate;
    UINT64                 Blocks;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Candidate))) {
      continue;
    }
    if (Candidate->Media == NULL || Candidate->Media->BlockSize != 4096) {
      continue;
    }
    Blocks = Candidate->Media->LastBlock + 1;
    // Exact sizes only: the userdata partition or the whole LUN0.
    if (Blocks == LOG_USERDATA_BLOCKS) {
      Bio = Candidate;
      Lba = LOG_WINDOW_PART_BLOCK;
      break;
    }
    if (Blocks == LOG_LUN0_BLOCKS && Bio == NULL) {
      Bio = Candidate;
      Lba = LOG_WINDOW_BLOCK;
    }
  }
  FreePool (Handles);
  if (Bio == NULL) {
    return EFI_NOT_FOUND;
  }
  Block = AllocateZeroPool (4096);
  if (Block == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Block, Line, Len);
  Status = Bio->WriteBlocks (Bio, Bio->Media->MediaId, Lba, 4096, Block);
  FreePool (Block);
  if (!EFI_ERROR (Status)) {
    Print (L"UniXNU npa: capture written at lba=%llx\n", Lba);
  }
  return Status;
}

VOID
UniXnuEnableTouchRails (
  VOID
  )
{
  VOID                *IfA;
  VOID                *IfB;
  VOID                *IfC;
  RAIL_DISPLAY_POWER  *Dp;
  UINT32               DispState;
  EFI_STATUS           SA;
  EFI_STATUS           SB;
  EFI_STATUS           SC;
  EFI_STATUS           DispGet;
  EFI_STATUS           DispSet;
  EFI_STATUS           Status;
  VOID                *NpaCliHandle;
  EFI_STATUS           NpaCliCreate;
  EFI_STATUS           NpaCliRequest;
  const char          *NpaCliName;
  CHAR8                Line[224];
  UINTN                Len;

  IfA = NULL;
  IfB = NULL;
  IfC = NULL;
  SA = gBS->LocateProtocol ((EFI_GUID *)&gNpaGuid, NULL, &IfA);
  SB = gBS->LocateProtocol ((EFI_GUID *)&gNpaAlt1, NULL, &IfB);
  SC = gBS->LocateProtocol ((EFI_GUID *)&gNpaAlt2, NULL, &IfC);
  Print (L"UniXNU npa: AE37B942 %r if=%p\n", SA, IfA);
  Print (L"UniXNU npa: 79D6C870 %r if=%p\n", SB, IfB);
  Print (L"UniXNU npa: 276732AB %r if=%p\n", SC, IfC);

  // Read the display power state; when the protocol exists but was never asked
  // for On (the panel is lit by the bootloader GOP), request it so the board's
  // panel power path runs, including its touch-rail client.
  Dp = NULL;
  DispState = 0xffffffffu;
  DispSet = EFI_UNSUPPORTED;
  DispGet = gBS->LocateProtocol ((EFI_GUID *)&gDisplayPwrGuid, NULL, (VOID **)&Dp);
  if (!EFI_ERROR (DispGet) && Dp != NULL) {
    DispGet = Dp->GetDisplayPowerState (Dp, &DispState);
    if (!EFI_ERROR (DispGet) && DispState != 2) {
      DispSet = Dp->SetDisplayPowerState (Dp, 2);
    }
  }
  Print (L"UniXNU disp: state=%x get=%r set=%r\n", DispState, DispGet, DispSet);
  Print (L"UniXNU rails: arbiter version=%08x\n", MmioRead32 (RAIL_CORE_BASE));
  RailEnable (L"pm8998_l6", RAIL_PPID_LOA6);
  RailEnable (L"pm8998_l28", RAIL_PPID_L28);

  // Vote the board's touch supply through the same NPA client API UsbConfigDxe
  // uses for its own rail. Two resource names exist in the firmware config; the
  // first that creates a client is asked for the active state (2).
  NpaCliCreate = EFI_UNSUPPORTED;
  NpaCliRequest = EFI_UNSUPPORTED;
  NpaCliHandle = NULL;
  NpaCliName = "none";
  if (!EFI_ERROR (SB) && IfB != NULL) {
    static const char *names[2] = {"/pmic/client/touch_screen", "/pmic/client/disp_prim_touch"};
    for (unsigned i = 0; i < 2; ++i) {
      EFI_STATUS Create;
      NpaCliHandle = NULL;
      Create = ((RAIL_NPA *)IfB)->CreateClient (names[i], "UniXNU", 0x40, 0, NULL, &NpaCliHandle);
      NpaCliCreate = Create;
      if (!EFI_ERROR (Create) && NpaCliHandle != NULL) {
        NpaCliName = names[i];
        NpaCliRequest = ((RAIL_NPA *)IfB)->Request (NpaCliHandle, 2);
        break;
      }
    }
  }
  Print (L"UniXNU npa client: %a create=%r req=%r\n", NpaCliName, NpaCliCreate, NpaCliRequest);

  // One ASCII line in the log window so the result survives without the screen.
  Len = AsciiSPrint (
          Line,
          sizeof Line,
          "UP1NPA3 npaA=%x npaB=%x npaC=%x disp=%x get=%x set=%x cli=%a cre=%x h=%lx req=%x\n",
          (UINT32)SA, (UINT32)SB, (UINT32)SC,
          (UINT32)DispState, (UINT32)DispGet, (UINT32)DispSet,
          NpaCliName, (UINT32)NpaCliCreate, (UINT64)(UINTN)NpaCliHandle, (UINT32)NpaCliRequest
          );
  Status = (Len == 0) ? EFI_BAD_BUFFER_SIZE : RailWriteCapture (Line, Len);
  Print (L"UniXNU npa: capture %r\n", Status);
}
