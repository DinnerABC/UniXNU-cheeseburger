# 触摸供电：UEFI 侧打开 pm8998 L6 / L28

`fb-rail` 候选的固件改动。回滚点：tag `rollback-before-fb-rail`（`390a608`）。

## 为什么是这两个稳压器

- 设备自身 FDT（`out/p21/touch-audit/recovery-device.dts`）：
  `pm8998_l6` 与 `pm8998_l28` 是 `qcom,rpm-smd` 资源（`resource-name=ldoa`，
  `resource-id=6 / 0x1c`，1.808 V / 3.008 V，`qcom,set=<3>`）。原厂驱动
  （Recovery dmesg）在 2.4 s 打开它们并探测到 0x20 上的 S3320。
- 本机 XNU 触摸驱动在释放复位后第一次 I2C 传输得到 QUP status bit8（NACK）：
  地址在 Recovery 已验证可用，复位已释放（`reset=1`），所以最可能是没有供电。

## 实现

`UniXNURails.c` 加进项目自有的 `UniXNUPkg/Driver/UniXNUDxe`（DXE 阶段、BDS
之前运行），通过 SPMI PMIC arbiter 只做两件事：

1. 只读：按 APID 表找到 PPID `0x045` / `0x05B`，读 LDO 的 VSET（0x40）与
   enable（0x46）。
2. 仅当 VSET 非 0 且 enable bit7 为 0 时，写 enable 寄存器置 bit7；随后回读，
   全部结果打印到控制台。

寄存器与命令格式依据：

- 设备 FDT：arbiter core `0x0800f000`、chnls `0x08400000`、obsrvr `0x09400000`，EE 0；
- 上游 sheng 固件的 SPMI 读实现
  （`upstream/uefi-sheng/Platforms/Xiaomi/shengPkg/Drivers/ShengKeyInputDxe/ShengSpmiKeys.h`）：
  按 PPID 查 APID、读命令 `(1<<27)|(reg<<4)`、状态 done bit0、数据 +0x18；
- Linux `spmi-pmic-arb.c` 的 v2 命令格式 `(opcode<<27)|((addr&0xff)<<4)|(bc&7)`
  与通道布局（cmd 0x00 / status 0x08 / wdata0 0x10 / rdata0 0x18，
  `0x8000*apid + 0x1000*ee`）；
- PM8998（PMIC4）LDO 页 `0x4000 + 0x100*id`：VSET +0x40、enable +0x46（bit7）。

不做的事：不读/写其它 PMIC 寄存器，不改电压，不碰 RPM、不写分区。

## 应用

```sh
python3 rebuild/ipados-up1/fb-rail/apply.py /home/<user>/unixnu-up1-fb-touch/source
```

脚本是幂等的，只改 `UniXNUPkg/Driver/UniXNUDxe/{UniXNUDxe.c,UniXNUDxe.inf}` 并
复制 `UniXNURails.c`。回滚时删掉这三个改动（或从 tag 恢复固件源码层）。

## 验收

启动时控制台应出现两行 `UniXNU rails: ... vset=.. en=.. write=.. en_after=..`；
随后 XNU 日志里 `TOUCH i2c` 计数增加、`error` 不再是 `-30`、出现 F01/F12。
