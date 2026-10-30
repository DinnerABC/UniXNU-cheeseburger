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

VOID
UniXnuEnableTouchRails (
  VOID
  )
{
  VOID        *If;
  EFI_STATUS   Status;

  If = NULL;
  Status = gBS->LocateProtocol ((EFI_GUID *)&gNpaGuid, NULL, &If);
  Print (L"UniXNU npa: AE37B942 %r if=%p\n", Status, If);
  If = NULL;
  Status = gBS->LocateProtocol ((EFI_GUID *)&gNpaAlt1, NULL, &If);
  Print (L"UniXNU npa: 79D6C870 %r if=%p\n", Status, If);
  If = NULL;
  Status = gBS->LocateProtocol ((EFI_GUID *)&gNpaAlt2, NULL, &If);
  Print (L"UniXNU npa: 276732AB %r if=%p\n", Status, If);
  Print (L"UniXNU rails: arbiter version=%08x\n", MmioRead32 (RAIL_CORE_BASE));
  RailEnable (L"pm8998_l6", RAIL_PPID_LOA6);
  RailEnable (L"pm8998_l28", RAIL_PPID_L28);
}
