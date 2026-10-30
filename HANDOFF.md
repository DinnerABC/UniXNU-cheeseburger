# UniXNU-cheeseburger · iPadOS 17.7.11 交接

下一步首帧前黑屏排查流程见 `docs/P21_BLACK_SCREEN_NEXT_STEPS.md`；其中返回码勘误覆盖下方旧分析。

## 2026-10-31：HID=0 的真因是独立 digitizer 设备覆盖了合并设备的 sink

- 日志 `20261030T170145Z`（boot `0x50...`，用户已触摸）：触摸解出 `contacts=1 publish=363`（`raw=77800000000`），但 `HID=0`；同一轮 `DISPLAY frames=0`，`[fb events]` 是已知的间歇分支 `E03 sel6 (0,0x80000003)` → `E04 rc=e00002c7`，与触摸改动无关（同镜像会翻转，此前基线也是这样）。
- 代码核对：`cheeseburger_touch_hid_start()` 走的是 `native_hid_touch_allocate()`（`touchOnly`），而 `NativeHidInput` 注释写明：实机上事件驱动附着的是**键盘+digitizer 合并设备**（`sheng-touch-hid=1`），独立 digitizer 设备是 opt-in 的 `sheng-touch-split`。`HidInputProbe` 先启动合并设备并装上 sink，触摸 worker 随后又启动独立设备，把 sink 覆盖成那个没有事件驱动的设备，于是 `touchSend` 早退、`HID` 永远 0。
- 修正（提交 `...`）：新增 `cheeseburger_touch_sink_ready()`；worker 在 `native_hid_stage==3` 之后**已有 sink 就不再创建独立设备**。同时加了四个计数器区分路径：`TOUCH19 sink/send/nodrv/rep`（sink 安装后的 publish 次数、touchSend 次数、因无事件驱动早退次数、成功取报告次数）。
- 候选镜像 `674381a9…`（内核 `b354e0ad…`、payload `e940c727…`、固件 `d6352763…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：下一轮 `TOUCH19 sink>0 send>0 nodrv=0 rep>0` 且 `HID>0` 即打通；若 `nodrv>0`，说明合并设备的事件驱动也没附着，需要回到 HID 描述符/匹配属性；若桌面本轮仍不亮，属显示间歇分支，同镜像再启动一次即可对照。

## 2026-10-31：触摸已解出坐标；HID 只是因为缺 `sheng-touch-hid=1` 没被启用

- 日志 `20261030T164836Z`（boot `0x501a...`，用户已实际触摸）：`TOUCH stage=4 error=0 i2c=20674/0 F01=2a F12=47`、`range=1079x1919 objects=10 data=6/80`、`frames=10317`、**`publish=348`**，最后一帧 `raw=69c00000284` 即 x=644、y=1692（都在 0..1079 / 0..1919 内），`mapped` 也已生成；`TOUCH19 nz=0` 说明所有非零数据包都成功解码，没有“有数据但解不出”的情况。
- `contacts=0` 只是日志抓取时最后一帧已抬手（`publish` 记录的是变化次数）；触摸本身已验证。
- **`HID=0` 的根因**：`NativeHidInput` 只有在启动参数 `sheng-touch-hid=1` 时才安装触摸 sink（`native_touch_set_sink`）并加入 digitizer 描述符；候选 `config/unixnu.toml` 的 args 里没有这一项，所以 `publish()` 的 sink 为空，事件没有进入 HID。已在 `cheeseburger-touch=1` 后加 `sheng-touch-hid=1`（args 从 644 增到 662 字节）。
- 候选镜像 `2dc35c79…`（内核沿用 `b1269b4e…`、payload `7dada6e2…`、固件 `6c9be14a…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：本轮日志的 `TOUCH` 行 `HID` 应 >0；桌面应能响应点按/滑动。若 `HID>0` 但界面无反应，下一步查 `IOHIDDevice::handleReport` 返回值与 backboardd 的 digitizer 匹配（`Built-In`/`DigitizerCollectionDispatch`）。

## 2026-10-31：触摸链路打通到 stage=4；只差接触数据

- 日志 `20261030T163537Z`（boot `0x5018e76c`）：`TOUCH stage=4 error=0 i2c=30285/0 F01=2a F12=47`，`range=1079x1919 objects=10 data=6/80`，`frames=15122`。F01/F12 发现成功，F12 的 80 字节数据包（10 指 ×8，跨 64 字节 FIFO 的块模式读）连续 6 分钟 30285 次传输、0 失败。
- 车 `max_x=1079 / max_y=1919` 与原厂 dmesg 的 `max_x=1080, max_y=1920`（最大索引 1079/1919）一致，`data1=0x06` 也与 F12 data base 一致。
- `contacts=0 publish=0 HID=0`：没有任何一次解出接触。此时无法区分“没摸屏幕”和“状态字节/偏移对不上”，因此加了一个极小探针（提交 `ff...`：「Capture the first non-zero F12 packet when no contact decodes」）：只要包里有非零字节但没解出接触，就把前 16 字节和 F01 status 记到 `TOUCH19 nz/p0/p1/f01`；不影响原有读写路径。
- 新镜像 `d10f0d1c…`（内核 `b1269b4e…`、payload `f26f7f0c…`、固件 `546f1ba1…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY。
- 判读：本轮回 Recovery 后若 `TOUCH19 nz>0`，直接看 `p0/p1` 的对象状态字节（bit0=present，0x01 手指、0x02 笔、0x03 手掌、0x04 未分类），据此修正 `cb_decode` 的状态判断或对象偏移；若 `nz=0`，说明面板根本没上报，下一步查 F01 control/IRQ 使能与扫描使能。

## 2026-10-31：PDT 一直是对的；真正卡点是 F12 函数号写成了十进制 12

- fb-qup3 日志 `20261030T161738Z`（boot `0x500700cb`）的寄存器图（`TOUCH17/18`，单字节连续读）：
  - `0xDD..0xE2` = `47 00 15 06 01 12` → **F12**（query 0x47 / control 0x15 / data 0x06 / fn=0x12），与整机 dmesg 的 `F12_2D_QUERY_BASE=47 … DATA_BASE=6` 逐项一致；
  - `0xE3..0xE8` = `2a 25 0e 04 01 01` → **F01**（fn=0x01）；
  - `0xE9..0xEE` = `26 00 0a 00 21 34` → **F34 flash**（query 0x26 / ctrl 0x0a / data 0x00），与 dmesg 的 `F34_FLASH_*` 一致。
  - `0xFF` 连续三次回读都是 0，分体事务（地址写带 STOP + 无地址指针读）与合并事务返回相同 6 字节 → 寻址、锁存、字节序都正常。
- **更正上一轮判断**：之前说“6 字节反序”是我把打包值按高位优先解码的误判。`packBytes` 是低位在前，`0x120106150047` 就是 `47 00 15 06 01 12`；PDT 从 v2 重写后一直读得对，不需要任何反转。
- **真正阻塞**：`SynapticsRMI4.h` 的 PDT 扫描写成 `if(p[5]==12)r.f12=f;`，十进制 12 = 0x0C，永远匹配不到 fn=0x12，于是 `f12.query=0`，`cb_discover` 直接返回 `-32`。已改为 `p[5]==0x12`（F01 同时写 0x01），提交 `9b467b0`。
- 候选镜像 `c9739407…`（内核 `e111ae32…`、payload `a9d2f14e…`、固件 `68ff6168…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 本轮耗时（本机构建树）：增量内核 34 s、预链接 8 s、补丁 5 s、payload 10 s、VM 固件 70 s、打包+门禁 15 s ≈ **2.5 分钟**。
- 判读：下轮日志 `TOUCH` 行应从 `stage=2/error=-32` 前进到 `stage=3` 且 `F01/F12` 非零；若停在 `-33/-34`，看 F01 identity/status；停在 `-35/-36` 看 F12 query 与三个寄存器描述符；进入 `stage=4` 后看 `frames/publish/HID`。

## 2026-10-31：构建树搬到本机，单轮构建从约 28 分钟降到约 4 分钟

- **位置**：构建树在本机 `/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree`（1.3 GB，构建后约 2 GB；本机剩余约 79 GB）。NAS 的 `out/p21/fb-qup3/source-tree` 保留一份源码副本，候选的 `kernel/ payload/ uefi/ config/ logs/` 仍全部在 NAS，证据链不变。
- **同步方式（增量，不再整目录复制）**：改代码在仓库 `package/BoardRuntime/`，然后 `cp package/BoardRuntime/{CheeseburgerTouch.cpp,BootLog.c} <本机树>/package/BoardRuntime/`；构建驱动 `scripts/p21-compile-fb-qup3.py` 已把 `R` 指向本机树、`C` 仍是 NAS 候选，内核产出由脚本自己写回 `out/p21/fb-qup3/native/kernel.macho`。本机树里 `tree/kits/cheeseburger` 已重指到本机 `package`，`build/native-ram-block`、`build/native-keystore-events` 两个绝对软链也改成指向本机树。
- **DriverKit 头种子**：本机树重新 prepare 时缺 6 个头会去 GitHub 抓（SSL EOF 失败）；驱动改为优先用**本候选** NAS 树的 `.cache/xnu-work` 作种子（37 个文件齐全），fb-probe 只作兜底。
- **实测**：内核 `kernel.build` 111 s（NAS 冷态 709 s、热态 109 s，CPU 238 s 真正并行）；预链接 8 s；panic 补丁 5 s；payload 约 10 s；VM 固件 70 s；打包+门禁约 10 s → **整轮约 4 分钟**。之前判断的“预链接 7 分钟”是错读时间戳，实际只有 8 秒。
- 本轮探针镜像：内核 `bb5d32ca…`、payload `bad654b0…`、固件 `730e599f…`、镜像 `b41443d3…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。

## 2026-10-30：QUP v2 通了——PDT 读出真实字节，但 6 字节内容与原厂 dmesg 反序

- fb-qup3 日志 `20261030T150933Z`（boot `0x4a5fb48d`，70 rounds）：`TOUCH12 ack=1000000`（0x20 正常 ACK）、`w=0 r=0 page=0`、`i2c=21/0`（21 次成功、0 失败）、`TOUCH14 fifo=64/64 blk=16/16`、`TOUCH10 pdt=3421000a0026/101040e252a/120106150047`。I2C 读写事务已真正工作，`error=-32` 只是 `cb_discover` 找不到 F01/F12。
- **关键对照**：整机原厂 dmesg（`out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`）给出 `F12_2D_QUERY_BASE=47 / CMD=0 / CTRL=15 / DATA=6`、`F34_FLASH_QUERY_BASE=26 / CTRL=a / DATA=0`。把 `0xDD` 读到的 6 字节 `12 01 06 15 00 47` **整体反转**即 `47 00 15 06 01 12`，与 F12 的 query/command/control/data/function 五项逐项吻合；`0xE9` 的 `34 21 00 0a 00 26` 反转后 query=0x26、ctrl=0x0a、data=0x00 也吻合 F34。说明数据是真的，只是字节序与标准 PDT 相反。
- 本轮不加猜测，直接加判定探针（提交 `ef77b64`）：① 页寄存器回读——写 `0xFF=0x00` 读 0xFC 起 4 字节、写 `0xFF=0x01` 再读，页字节出现在第 4 位=顺序正常、出现在第 1 位=读流反序；② 偏移测试——`0xE8` 与 `0xE9` 各读 6 字节（顺序正常时 `e8[1..5]==e9[0..4]`），另读 `0xEE`；③ 按两种字段序（byte5 或 byte0 作函数号）扫描 0xE9→0x05，记录各自看到 F01/F12 的地址；④ 在 F12 query(0x47)/control(0x15) 各读 6 字节做内容验证。报告行新增 `TOUCH15`/`TOUCH16`。
- 候选 `out/p21/fb-qup3`（复用同一候选目录，旧证据保留在 `boot-once.log`/历史日志）：内核 `54bfa72a…`、payload `1b6e87dc…`、固件 `a4a2ca81…`、镜像 `b7eb4776…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。设备 `3b25cc7c` 临时 boot 一次，未写分区。
- 判读：若 `TOUCH15` 的页字节在 `pg0` 的第 4 字节 → 读流正常，问题在设备字段序，按反转字段解析 PDT 即可；若在第 1 字节 → 读流反序，需要修正传输层的字节顺序（下一步要确认是 per-word 还是整块）。`TOUCH16` 的 `f01n/f12n` 与 `f01r/f12r` 会直接给出两种解析各自的命中地址。

## 2026-10-30：根因更正——QUP5 是 v2.2.1，改用 v2 字节 tag 与重复起始读

- fb-probe2 日志 `20261030T133244Z`（boot `0x4ff8d59c`，93 rounds）：`TOUCH12 ack=0,0 w=0 r=-8 b=0`、`TOUCH10 pdt=0/0/0`、`TOUCH13 status=c110000`、`TOUCH hw=20070000`。写事务"完成"但整条总线没有任何地址 ACK、读超时，和 v1 编程的预期不符。
- **根因**：本机参考 DTS `out/p22/reference/arch_arm64_boot_dts_qcom_msm8998.dtsi` 中 `blsp1_i2c5` 的 compatible 是 `qcom,i2c-qup-v2.2.1`，同一份 `i2c-qup.c` 因此走 `qup_i2c_xfer_v2`，而不是上一轮照抄的 v1 路径：
  - `QUP_CONFIG = I2C_MINI_CORE|I2C_N_VAL_V2 = 0x207`（不是 0x20F），只写时 `|0x80`；
  - `QUP_I2C_MASTER_GEN(0x408)=1` 使能 v2 tag；缺这一步 tag 不会被解释；
  - FIFO 字节流是 `0x81 addr`、`0x82`(无 STOP)/`0x83`(STOP) `+len`、`0x87`(读 STOP)`+len`，数据按 4 字节/字裸排；v1 的 16-bit tag 半字在这里完全无效；
  - 寄存器读是**一次 transfer 的两条消息**：写地址（不 STOP）+ 读（STOP，重复起始），与 `rmi_i2c.c` 一致；上一轮拆成两次独立事务。
- 重写（提交 `925cc56`、`13b26cf`）：复位/状态机按参考实现（RESET→配置→RUN→写 clock_ctl→PAUSE→写 FIFO→RUN→等完成），reset 态读 IO_MODE 计算 FIFO/块大小，大读用 `IN_BLOCK_READ_REQ` 块握手，RX 首 2 字节是收到的 tag 需丢弃，第二个子传输的计数字段带 `QUP_I2C_MX_CONFIG_DURING_RUN(BIT31)`，结束前等待 `BUS_ACTIVE` 清零。诊断行同步为 `TOUCH12 … page=`、`TOUCH13 st/op/err/status`、`TOUCH14 fifo/blk/pre/pdt/cnt`。
- 构建链注意：候选树的 `tree/kits/cheeseburger` 是指向上一个候选 `package` 的绝对软链，复制候选后必须重指；预链接/门禁脚本需要 `python3.13`（`tomllib`）；固件复用 guest overlay `unixnu-up1-fb-touch`，pin 扫描显示 kernel/payload/ADT 三个哈希都进入 `FVMAIN.Fv` 与 `P9Probe.efi`/`DarwinBoot.efi`。
- 候选 `out/p21/fb-qup3`：内核 `fbdbb96043e451b2…`、payload `e6fa821a704ab466…`、固件 `1668d94d4249c8e9…`、镜像 `out/p21/fb-qup3/uefi/boot-p21-up1.img` `1c054d5e19a21dcd…`；pins 命中、门禁 PASS；设备 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区，`out/p21/fb-qup3/boot-once.log`；监视器 `scripts/unattended/monitor.py --timeout 3600` 已启动等待回收。
- 判读：`TOUCH12 ack` 中地址 0x20（从 0x08 起第 24 位，即 `m0` 的 `0x1000000`）应置位；`TOUCH10 pdt` 应出现 `fn=01`/`fn=12` 的合法条目；`TOUCH` 行 `i2c` 成功计数增长、`F01/F12` 非零；若 ACK 正常但 PDT 仍全 0，下一步查页选择与读块握手（`TOUCH14` 的 `fifo/blk` 与 `cnt`）。

## 2026-10-30：三轮审计发现探针根本错误（时钟未设），已修正并重测

- fb-probe 结果（`20261030T130947Z`，boot `0x50f62b74`）：`TOUCH12 ack=0,0 w=-6 r=-6`、`TOUCH10/11` 全 0、`i2c=0/12`。
- **审计第一轮（对照 i2c-qup.c）**：我方错误地把 `I2C_STATUS bit8` 当 NACK。上游 `I2C_STATUS_BUS_ACTIVE=BIT(8)`、`QUP_I2C_NACK_FLAG=BIT(3)`，错误掩码 `0x38000fc` 已含 NACK；该检查已删除，NACK 仍由 `status&8` 报 `-6`。
- **审计第二轮**：① 配置前遇 `BUS_ACTIVE` 直接 `-4`，一次失败会污染后续所有传输 → 改为 20 ms 有界等待；② 缺少上游每次配置都会写的 `QUP_OPERATIONAL_MASK(0x28)=0x300` → 已补。
- **审计第三轮（关键）**：三个探针块都位于 `clockCtl=0x300|93;` **之前**，`wr(qup,0x400,0)` 让 QUP 没有 SCL 时钟，所以 TOUCH9/10/11/12 的结果全部无效（发现阶段的 25 次成功才是有时钟的真实结果）。已把时钟设置提前到任何探针之前。
- **审计第四轮**：删除旧的 `0x81/0x82/0x87` 帧探针（在 tag 化传输下会向任意 RMI 寄存器写入数据）与两字节地址探针（会写命令寄存器 0x00）；探针改为对 0xE9 PDT 头做只读访问 + 全地址只读 ACK 扫描。错误掩码、报告行、数组边界均复核通过。
- 候选 `out/p21/fb-probe2`：内核 `8d54f5ea…`、payload `9caccca3…`、固件 `73a44d59…`、镜像 `a5292ff3…`；pins 命中、门禁 PASS；提交 `20c67f5`；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH12 ack` 现在才是有意义的 ACK 位图（0x20 位应置位），`TOUCH10` 应显示真实 PDT 字节；`TOUCH13` 的 `st/cnt` 用于确认装载与结束状态。

## 2026-10-30：两种地址形式都读回 0；改为一次性全面探针

- fb-pdt2 日志 `20261030T124405Z`（boot `0x51ca5b32`）：`TOUCH10 pdt=0/0/0`、`TOUCH11 pdt2=0/0/0` → 1 字节与 2 字节地址都拿不到数据，`error=-32`、`i2c=25/7`。
- 审计发现的判定缺口：`qupFinish()` 只看 status 的 `0x38000fc`，**没有把 NACK 位（`0x100`）当作错误**，所以"25 次成功"可能包含被 NACK 的写操作。
- fb-probe（提交 `1d7f7c4`）一次性补齐信息（单次启动）：
  1. `TOUCH12 ack=…`：对 0x08–0x77 全部地址做读探针，输出两个 64 位 ACK 位图（第一字=0x08–0x47，第二字=0x48–0x77）；
  2. `w=`/`r=`：写寄存器 0x00 指针、再读 8 字节的返回码，`b=` 为读到的 8 字节；
  3. `TOUCH13 st=`：读后 QUP `STATE/OPERATIONAL/I2C_STATUS/ERROR_FLAGS` 打包；
  4. `cnt=`：`MX_OUTPUT_CNT/MX_WRITE_CNT` 与 `MX_READ_CNT/MX_INPUT_CNT` 的实际寄存器值；
  5. NACK 现在会让事务返回 `-6`，日志里会直接体现被拒。
- 构建流程修正：GitHub 抓取间歇性 SSL EOF，驱动改为从已准备好的 `fb-pdt` 树**本地种子**恢复 DriverKit 头（哈希仍校验），不再依赖网络；`scripts/p21-compile-fb-probe.py` 可复用。
- 候选 `out/p21/fb-probe`：内核 `4800a1a0…`、payload `d450c215…`、固件 `a1c507a3…`、镜像 `27852270…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：ACK 位图里有 0x20 → 总线寻址正常，问题在寄存器/数据阶段；没有 0x20 → 从机地址或写事务本身不对。`cnt=` 显示 QUP 实际装载的计数能验证 tag/长度是否按预期进入控制器。

## 2026-10-30：PDT 原始字节全 0；对比 1 字节 / 2 字节地址

- fb-pdt 日志 `20261030T122342Z`（boot `0x4f772688`）：`TOUCH error=-32 i2c=25/4`、`TOUCH10 pdt=0/0/0`——页面 0 的 0xE9/0xE3/0xDD 三次读回全 0，且 I2C 状态没有错误位。
- 说明读事务"完成"但拿不到真实数据，怀疑地址形式（RMI 16 位地址是写 1 字节低地址，还是写"页,偏移"两字节）。
- fb-pdt2（提交 `2a89d52`）对同一寄存器做两种地址形式的对照，新增 `TOUCH11 pdt2=...`（两字节地址），与 `TOUCH10`（一字节地址）并列比较。
- 候选 `out/p21/fb-pdt2`：内核 `19e407a1…`、payload `cb3e935f…`、固件 `5a011a63…`、镜像 `7318ac0e…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `TOUCH11` 出现合法 PDT（fn=01/12），采用两字节地址；若两者仍全 0，说明问题在写事务本身（地址/START/STOP tag 或从机地址），下一步用 QUP 的 I2C_STATUS 逐位与上游 `I2C_STATUS_ERROR_MASK` 对照，并检查 0x40 是否被当作数据字节。

## 2026-10-30：读事务已通（i2c=25/0），PDT 仍无 F01/F12；已加原始 PDT 转储

- fb-qup2 日志 `20261030T120148Z`（boot `0x4fad997b`）：`TOUCH error=-32 i2c=25/0`、`status=c110000`、`op=a20`；`TOUCH5 raw=7 gpiopull=3 muxed=3`（固件阶段起 SDA/SCL 就已是高，说明 UEFI 的 NPA 投票在 XNU 之前就把触摸轨打开了）。
- 结论：I2C **读写事务都已工作**（25 次成功、0 失败），但扫描仍找不到 F01/F12。不再靠推断，改为把页面 0 顶部三个 PDT 表项（0xE9/0xE3/0xDD）的原始 6 字节读出来写入日志 `TOUCH10 pdt=a/b/c`。
- 构建流程记录：本机代理当前对 GitHub TLS 返回 EOF，而 `raw.githubusercontent.com` 直连可用；`scripts/p21-compile-fb-pdt.py` 因此不再注入代理（api.github.com 的清单已缓存，无需访问）。可重复使用。
- 候选 `out/p21/fb-pdt`：内核 `e00ba25b…`、payload `42aeb9fe…`、固件 `ddec1875…`、镜像 `out/p21/fb-pdt/uefi/boot-p21-up1.img` `9af808c5…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `TOUCH10` 里出现 `fn=01`/`fn=12` 的合法条目，说明扫描逻辑有偏差（如 break 条件）；若全 0/0xFF，则读到的地址内容不对（页选择或地址字节需再核对）。

## 2026-10-30：审计 fb-qup——写事务已通、读事务超时；改为整块排空后重测

- fb-qup 日志 `20261030T114202Z`（boot `0x4f9e7096`）：`TOUCH error=-30 i2c=3/3`、`status=c110000`、`op=820`、`lines sda=1 scl=1 rst=1`。
- 审计判断：`i2c=3` 成功说明**写事务（页选、写地址）在 tag 化之后已经正确**；读失败是因为 `qupFinish()` 每次 INPUT_SERVICE(`0x200`) 只读 1 个 FIFO 字（2 字节），6 字节读永远凑不满而超时。上游做法是：一个 REC tag 时整块数据已在 FIFO 中，服务标志一次即可全部读出，并以"字节收满"为完成条件（不依赖完成位）。
- 修正（提交 `25d28b0`）：INPUT_SERVICE 置位后清标志并循环排空 `ceil(nr/2)` 个字；`got==nr` 即成功并回 RESET。写事务仍以完成位判断。
- 候选 `out/p21/fb-qup2`：内核 `af541d8b…`、payload `da2b3baa…`、固件 `ecc6dea1…`、镜像 `out/p21/fb-qup2/uefi/boot-p21-up1.img` `410a8d18…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH` 的 `i2c` 失败数应变为 0，随后 `F01/F12` 非零、`stage` 前进；若读仍失败，用 `status/errors/op` 与 `TOUCH9` 判断是 FIFO 排空过多（读到 0）还是 REC tag 长度问题。

## 2026-10-30：QUP 传输已按上游重写（tag 化写 + 独立读事务），fb-qup 已启动

- 依据上一轮的 5 点差异（提交 `6feceba`），把 `CheeseburgerTouch.cpp` 的 I2C 层重写为上游 `i2c-qup.c` 的 v1 编程：
  - `QUP_CONFIG`：写 `0x28F`（`I2C_MINI_CORE|15|NO_INPUT`），读 `0x20F`；
  - `QUP_IO_MODE 0x8`：`REPACK_EN(0xC000)`，块模式才加 `OUTPUT_BLK_MODE(1<<10)` / `INPUT_BLK_MODE(1<<12)`；
  - OUT FIFO 每 32 位字装两个半字，半字 = `(tag<<8)|byte`：START `0x100`（含从机地址 0x40/0x41）、DATA `0x200`、STOP `0x300`、REC `0x400`；
  - 写事务：START|addrW、数据/STOP tag，`MX_WRITE_CNT=len+1`；
  - 读事务：OUT FIFO 写 `(START|addrR)|((REC|len)<<16)`，`MX_READ_CNT=len`，IN FIFO 每字取 2 字节；
  - 顺序：RESET → RUN(写 clockCtl) → PAUSE(写 tag) → RUN → 轮询完成；错误位与 cb_touch 计数保持原有诊断。
- 这样 `cb_wire_page`/`cb_wire_read`/`writeRmi` 的既有语义（页选 + 写地址 + 读）在 RMI 标准下才成立。
- 候选 `out/p21/fb-qup`：内核 `13eeb0ae…`、payload `84f9d893…`、固件 `31a54194…`、镜像 `out/p21/fb-qup/uefi/boot-p21-up1.img` `26f4a3c9…`（82,575,360 bytes）；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH` 行出现 `i2c>0` 且 `F01/F12` 非零 → RMI 通了；若 `TOUCH9 plain` 仍为负，说明写事务或 tag 仍有偏差，下一步用 `TOUCH` 的 `status/errors/op` 三元组定位到具体阶段。

## 2026-10-30：双帧取样定因——QUP 传输本身不符合上游编程

- fb-frame 结果（`20261030T112102Z`，boot `0x4d540669`）：`TOUCH9 plain=fffffff9 old=a002600000000`
  - 纯地址帧：返回码 **-7**（QUP 控制器错误），数据全 0；
  - 旧帧：返回码 0，但数据 `00 26 0A 00` 不是合法 PDT 头。
- 对照 `out/p22/reference/drivers_i2c_busses_i2c-qup.c`（设备原厂驱动）逐项核对，发现我们的 QUP 编程有多处偏差，这才是 RMI 读不出来的根因：
  1. `QUP_CONFIG` 的 `I2C_N_VAL` 应为 **15**（`I2C_MINI_CORE|15 = 0x20F`，只写时再 `|0x80`）；我们写的是 `0x207`；
  2. OUT/IN FIFO 的每个 32 位字必须带 **tag**：低半字 `START(0x100)|addr8`，数据 `DATA(0x200)`/`STOP(0x300)`，读请求 `REC(0x400)|len`，第二个字节在 bit16 起；我们的代码直接往 FIFO 写裸数据，没有 tag、也没有从机地址；
  3. 读是**独立的 QUP 事务**：OUT FIFO 写一个字 `(START|addrR) | ((REC|len)<<16)`，`MX_READ_CNT(0x208)=len`；我们从未这样编程；
  4. `QUP_IO_MODE(0x8)` 应为 `REPACK_EN(0xC000)` 加必要的 `OUTPUT_BLK_MODE(1<<10)`/`INPUT_BLK_MODE(1<<12)`；我们只写 `0xC000|0x1000`；
  5. 状态时序：RESET→RUN→(写 clockCtl)→PAUSE→(写 FIFO tags)→RUN→等待完成；我们的顺序接近但 FIFO 内容错误。
  - 结论：之前所谓的"21 次 ACK 成功"是没有真正寻址的完成事件，不能当作总线可用。
- 下一步（新候选）：按上述 5 点重写 `CheeseburgerTouch.cpp` 的 `transfer()` 为上游 write_op/read_op 两个事务（含 tag 编码、按 FIFO 大小分块、IN FIFO 每字读 2 字节），然后 `cb_wire_page`/`cb_wire_read`/`writeRmi` 用"写地址（独立事务）+ 读（独立事务）"的 RMI 标准访问。完成后仍用 `TOUCH9` 和 `TOUCH` 行验证。

## 2026-10-30：标准 RMI 帧反而更早失败；双帧取样探针已启动

- fb-rmi 结果（`20261030T110354Z`，boot `0x4e5a3ae7`）：NPA 投票仍成功（捕获块同前），SDA/SCL 仍为 1/1，但触摸在第一次 RMI 读就失败：`error=-30 i2c=0/1`、`status=c960000`（旧帧时是 `1321c4`，且能完成 21 次 ACK）。说明"标准地址帧"在我们这套 QUP 传输上没有正确发出，而旧的 `0x81/0x82/0x87` 帧虽能 ACK 但返回数据不可用。
- 为避免继续在两种帧之间猜（提交 `3eb2419`）：`hardware()` 在 QUP 初始化后、`cb_discover()` 之前，对同一地址（页 0 的 0xE9 PDT 头）分别用两种帧各读一次，把返回码与前 4 字节写进 `cb_touch_extra[24]/[25]`，日志新增 `TOUCH9 plain=.. old=..`（低 32 位=传输返回码，其后每字节依次为数据）。
- 候选 `out/p21/fb-frame`：内核 `8d1367bd…`、payload `4312eaba…`、固件 `094d5d25…`、镜像 `ec30620e…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH9` 里哪一种帧返回的 6 字节呈现合法 PDT（如 fn=0x01/0x12、基址合理）就采用哪种，并据此修正 `cb_wire_read` 或 QUP 的读写时序；若两种都拿不到合法数据，则转向修正 QUP 的 combined write→read 时序（对照 `out/p22/reference/drivers_i2c_busses_i2c-qup.c`）。

## 2026-10-30：触摸供电已解决（NPA 投票成功），RMI 传输按上游修正

- NPA 投票结果（`20261030T104504Z`，boot `0x50d41a7b`）：
  `UP1NPA3 ... cli=/pmic/client/touch_screen cre=0 h=1B44F2ADC req=0` → 客户端创建成功、请求被接受；
  XNU 侧 `TOUCH lines sda=1 scl=1 rst=1 int=0`（此前一直 0/0），`TOUCH2 pins=7`，**I2C 传输 21 次成功 0 次失败**。**触摸供电问题解决。**
- 新的失败点：`TOUCH error=-32 i2c=21/0 F01=0 F12=0` → `cb_discover` 的 `if(!r.f01.query||!r.f12.query)return -32;`，即 PDT 扫描没找到 F01/F12。核对 `out/p22/reference/drivers_input_rmi4_rmi_driver.c`：PDT 字段（buf[0]=query、buf[5]=function number、buf[4]&0x1f=中断数）与扫描方向（0xE9→0x05，步长 6）**我们的实现与上游一致**；差别在 I2C 帧格式。
- 修正（提交 `ab0614f`）：`SynapticsRMI4.h`/`CheeseburgerTouch.cpp` 的 RMI 传输改为上游 `rmi_i2c.c` 的标准帧——页选写 `{0xFF, page}`，读为"写低地址字节 + 读 n"，写为"低地址字节 + 数据"，替换原先自定义的 `0x81/0x82/0x87` 帧。
- 候选 `out/p21/fb-rmi`：内核 `1d2730ad…`、payload `f9bafc4d…`、固件 `ab7dd8d7…`、镜像 `out/p21/fb-rmi/uefi/boot-p21-up1.img` `2c3f9f5f…`（82,587,648 bytes）；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `TOUCH` 行出现 `F01`/`F12` 非零、`stage=4`、`HID>0`，则 RMI 与 HID 都通了，可以开始验证触摸坐标；若仍 `-32`，下一步在固件侧或驱动里直接读 PDT 头 6 字节做原始核对（页面 0 的 0xE9 项）。

## 2026-10-30：DisplayPwr 未安装；从 UsbConfigDxe 拿到 NPA 客户端 ABI，已投票触摸供电

- 上一轮结果（`20261030T103345Z`，boot `0x50872df8`）：捕获块 `UP1NPA2 ... disp=FFFFFFFF get=E set=3` → **DisplayPwr 协议未安装**（`E` = NOT_FOUND），该路径关闭；XNU 触摸仍 `-30`、SDA/SCL 仍 0。
- **关键突破（照抄固件现有调用，不自己写 RPM）**：`UsbConfigDxe` 用同一 NPA 协议（`79D6C870…`）给 `/pmic/client/usb_ss1` 投票，反汇编得到 ABI：
  - `[iface+0x10]` 创建客户端：`(resource_name, client_name, type=0x40, param=0, extra=NULL, &handle)`；
  - `[iface+0x28]` 发请求：`(handle, state_u32)`（USB 用 state=2 表示 active）。
- 据此在固件启动时：先尝试 `/pmic/client/touch_screen`，失败再试 `/pmic/client/disp_prim_touch`，创建成功后发 state=2；结果写入捕获块 `UP1NPA3 ... cli=<name> cre=<status> h=<handle> req=<status>`。仍不调用任何未知偏移、不写 PMIC 寄存器。
- 提交 `6e89bf8`。镜像 `out/p21/fb-diag8/uefi/boot-p21-up1.img` `9eaf0a0d…`；固件 `eb604705…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `cre=0` 且 XNU 侧 `TOUCH lines sda/scl` 变 1，则供电已通、触摸应开始 ACK；若 `cre` 非 0，说明这两个客户端名不是 NPA 资源，需改用 `/pmic/client/<ldo>` 形式的资源名或走 DAL 客户端节点。

## 2026-10-30：NPA/DAL 逆向进展 + DisplayPwr 实验

- 更正上一轮的 GUID 归属（重新核对 `/tmp` 反汇编与二进制）：
  - `79D6C870-725E-489E-A0A1-27E7A5D0CB35` 是 **NpaDxe 安装的协议**（PmicDxe 定位它、变量名叫 NpaProtocol；表在 NpaDxe `.data:0x180F0`，`+0x90 = 0x1864` 是进入 `0x48f0` 的 thunk，参数按对象而非字符串解释——说明它先要资源对象，再建客户端）；
  - `AE37B942-457F-4C91-A196-D9669FD347A3` 由 **DALSys 安装**（`.data:0x1B050`，接口表 `0x1B090`）；
  - `276732AB-BB39-4DCE-B5D9-2746CDD6D518` 也是 NpaDxe 安装（接口表 `0x182A0`）。
- **DALSys 内含现成的电源节点**（`.efi` 字符串）：`/pmic/device/ldo/A/ldo6/vec`、`/pmic/device/ldo/A/ldo28/vec`、`/node/pmic/client/disp_prim_touch`、`/pmic/client/touch_screen`——即固件 DAL 配置里已经知道这两路 LDO 和触摸供电路径；缺的是从我们的 DXE 调用 DAL 的方式。
- 已找到并采用**有本地头文件依据**的接口：`EFI_DISPLAY_POWER_PROTOCOL`（GUID `F352021D-9593-4432-BF04-67B9F3B76008`，结构 `{Revision, SetDisplayPowerState, GetDisplayPowerState}`，来自 `Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIDisplayPwr.h`）。本轮固件在启动时读取显示电源状态，若存在且不为 On 就请求 On，让其面板上电路径（含 `disp_prim_touch` 客户端）执行；结果写入日志窗口第 8161 块（`UP1NPA2 ... disp=.. get=.. set=..`）。
- 分支提交：`717edf5`（NPA 可用）、`57edcfd`（DisplayPwr 探测）。镜像 `out/p21/fb-diag8/uefi/boot-p21-up1.img` `e6bc9806…`；固件 `7b9d2042…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若回收块里 `disp` 从非 2 变为请求 On 且 XNU 侧 `TOUCH lines sda=1 scl=1`，说明面板上电路径确实包含触摸轨；若仍为 0，则下一步按 NpaDxe/DAL 的对象式 API 继续（需要先找到资源对象获取函数，再 issue request）。

## 2026-10-30：NPA 协议运行时可用（捕获块读取）

- `20261030T102044Z` 的 `window.bin` 第 8161 块（UEFI 写入的保留块）内容：
  `UP1NPA1 npaA=0 ifA=1B57BC090 npaB=0 ifB=1B57660F0 npaC=0 ifC=1B57662A0`
- `npaA/B/C = 0` = EFI_SUCCESS：`AE37B942-457F-4C91-A196-D9669FD347A3`（PmicDxe 称为 NpaProtocol）、`79D6C870-725E-489E-A0A1-27E7A5D0CB35`、`276732AB-BB39-4DCE-B5D9-2746CDD6D518` 在运行时**都已安装**；NPA 接口指针 `0x1B57BC090`。
- 同轮 `20261030T101551Z`（boot `0x510d8b69`）进入桌面 `frames=8`；触摸仍 `-30`、SDA/SCL 仍为 0。
- 结论：**方案 A 可行**——下一轮可按 PmicDxe 的方式（`LocateProtocol(AE37B942)` → `[iface+0x90]` 三参数）创建 NPA 客户端，再找到 issue-request 的 vtable 偏移与 LDO 资源名，向 RPM 请求 L6/L28 上电。
- 通道已固化：UEFI 结果写窗口第 8161 块（绝对 LBA 5071073），无需看屏幕；本轮验证了写入与回收链路都正常。

## 2026-10-30：方案 A 启动——离线确认 NPA/PMIC 接口，fb-diag8 只读探测已启动

- 触摸供电结论（已确认）：Recovery 里原厂内核把 `pm8998_l6`（1,808,000 µV）和 `pm8998_l28`（3,008,000 µV）置为 enabled、各有 1 个使用者；而我们的 fastboot→UEFI→XNU 路径没有任何 RPM 投票，SDA/SCL 在未写之前就是低、~50 kΩ 内部上拉拉不动、8 mA 强驱动能顶高——未上电芯片钳位。AP 的 SPMI arbiter（v3，244 项）看不到 0x40–0x5f 的 LDO 页，**从 AP 直接写 PMIC 不可行**。
- 离线固件分析（`rebuild/ipados-up1/npa-audit/`）：
  - 从 27 个 `.depex` 反推出真实协议 GUID 词表；`AE37B942-457F-4C91-A196-D9669FD347A3` 被 PmicDxe 以字符串“NpaProtocol”定位，且被 AdcDxe/ClockDxe/I2C/SPI/DAL 等 9 个驱动依赖 → 即 NPA 协议；
  - 反汇编 PmicDxe `0x15b3c`：`LocateProtocol(&AE37B942…, NULL, &iface)` 后 `blr [iface+0x90]`，三参数（名称、类型、客户端名）；PmicDxe 内含 `pm_ldo.c`/`pm_ldo_driver.c`/`pm_smps_driver.c` 与 `/protocol/rpm/rpm`，即 LDO 通过 NPA→RPM 控制；
  - NpaDxe `0x1548/0x1594` 安装的是 `79D6C870-725E-489E-A0A1-27E7A5D0CB35`（接口表 `0x180F0`）与 `276732AB-BB39-4DCE-B5D9-2746CDD6D518`（`0x182A0`）；`0x12FC4` 处只定位 `AE37B942`。因此 `AE37B942` 的安装者仍待确认。
- 本轮代码（提交 `d61bbd9`，两轮审计后编译）：`UniXNURails.c` 新增**只读** `LocateProtocol` 探测，启动时打印三行 `UniXNU npa: <GUID> <status> if=<ptr>`；不调用任何接口、不写寄存器。
- 候选 `out/p21/fb-diag8`（内核/payload 与 fb-diag7 相同）：固件 `ad87e327…`、镜像 `out/p21/fb-diag8/uefi/boot-p21-up1.img` `9f05cb55…`（82,591,744 bytes）；pins 命中、探测字符串已确认编入 `UniXNUDxe.efi`（3 处）、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 追加（提交 `fac2475`、`dc2ccf5`）：UEFI 探测结果不再依赖屏幕——`RailWriteCapture()` 把一行 ASCII（`UP1NPA1 npaA=… ifA=… npaB=… ifB=… npaC=… ifC=…`）写进持久日志窗口的第 **8161** 块（绝对 LBA 5071073 / userdata 相对 2629601）。该块属于窗口尾部 31 个内核不使用的块（内核只用 0..8160），按精确块数匹配 userdata（11703808 块）或 LUN0（14145536 块）后才写，找不到就不写。构建时发现并修复 INF 缺少 `gEfiBlockIoProtocolGuid` 的链接错误。
- 判读：屏幕上若出现 `npa: AE37B942 Success if=0x…`，说明 NPA 协议可用，下一轮按 PmicDxe 的调用方式实现 `create_client`/`issue_request`；若全部 `Not Found`，方案 A 需要改走别的协议或退回方案 B（XNU RPM-SMD）。

## 2026-10-30：fb-diag6 结果——写位正确、线路被外部钳低；fb-diag7 驱动实验已启动

- fb-diag6 日志 `20261030T092128Z`（boot `0x5039ae38`，67 轮）：进桌面 `frames=8`；触摸仍 `-30`。
  - `TOUCH5 raw=0 gpiopull=0 muxed=0`：**在任何写之前** SDA/SCL 就是低；用正确的 pull 位（[1:0]）仍拉不高；
  - `TOUCH6 cfg87=c cfg88=c cfg89=2c3 cfg125=3`：mux/上拉/OE 写入**确实生效**（87/88 = mux3、无上拉；89 = mux0/8 mA/上拉/OE；125 = mux0/上拉）；
  - `TOUCH7 post qup0=0 qup400=0 qup408=0 qupc=0`：结合代码路径可知 `transfer()` 在第一次 `state(0)` 就超时返回，后面的 QUP 配置写根本没执行——QUP 状态机没有进入 idle。
- 综合判断（推断）：SDA/SCL 被外部强制钳低（内部上拉无效），同时 QUP 无法完成状态转换。最可能仍是触摸 IO 供电缺失（未上电芯片用 ESD 二极管把两条线钳在 0 V），其次是总线被短接；PM8998 的 regulator 页不归 AP EE，**只能走 RPM 投票**。
- fb-diag7（提交 `fc26e25`）增加两个判定实验：
  1. `TOUCH8 drive=..`：把 gpio87/88 切成 GPIO 输出、驱动高电平约 1 ms 后回读——回读 0 = 外部强钳低（供电缺失/短路），回读 1 = 线是自由的，之前的低电平来自我们自己的配置；
  2. `TOUCH8 bc=..`（GCC_BLSP1_QUP5_BCR `0x21000`）与 `TOUCH3 qup0state=..`（QUP CONFIG + STATE）用于判断 QUP 是否被 block reset 或时钟没有真正生效。
- 候选 `out/p21/fb-diag7`：内核 `84638732…`、payload `a0fa5dba…`、固件 `2d81762e…`、镜像 `out/p21/fb-diag7/uefi/boot-p21-up1.img` `1684dd86…`（82,591,744 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。

## 2026-10-30：fb-diag5 结果 + 两遍代码审计（发现 pull 位错误）+ fb-diag6 已启动

- fb-diag5 日志 `20261030T085525Z`（boot `0x50eb51b4`，76 轮）：进桌面 `frames=9`；触摸 `-30` 不变。
  - `TOUCH5 raw=0 gpiopull=0 muxed=4`；
  - 修正后的 PMIC 行：`PMIC entries=244 sid0=12 pages=1`（只有 `0x61` 一个 0x40–0x7f 页，`vset=00 en=01`）。
- **审计发现（第一遍，已确认的错误）**：
  1. `TOUCH5 raw` 采样点在 I2C 复用**之后**，并非"固件原状"，标签失真；
  2. 内部上拉实验写的是 bits 10-11，而设备原厂 `pinctrl-msm8998.c` 明确 `pull_bit = 0`（bits 1:0）、`mux_bit=2`、`drv_bit=6`、`oe_bit=9`、`in_bit=0`、`out_bit=1`。所以 `gpiopull=0` **不能**证明外部钳位，该实验无效；
  3. QUP `0x0/0x400/0x408/0xC` 在首次传输前读取，必然为 0，信息量低；
  4. 复位脚与中断脚没有按原厂 `ts_reset_active`/`ts_int_active` 的 `bias-pull-up` 配置。
- 时钟/地址复核（第二遍，已确认正确）：`gcc_blsp1_qup5_i2c_apps_clk` CBCR `0x21008` bit0、RCG `CFG_RCGR` `0x21024`、`gcc_blsp1_ahb_clk` `0x52004` bit17（halt `0x17004` bit31）均与设备原厂 `gcc-msm8998.c` 一致；数组边界（`cb_touch[28]`/`cb_touch_extra[24]`/`cb_pmic_dump[24]`/`cb_pmic_pages[8]`/`cb_rails[24]`）互不重叠。
- fb-diag6（提交 `cb27e11`）修复上述四点：raw 采样提前到任何写操作之前、独立 `muxed` 采样、pull 位改为 [1:0]（复位脚/中断脚同时按原厂加上拉）、QUP 高偏移寄存器改在 `fail:` 时（首次真实传输失败后）读取，新增 `TOUCH7 post ...` 行；报告缓冲区 160→200。
- 候选 `out/p21/fb-diag6`：内核 `e0855e4f…`、payload `c3287e0a…`、固件 `b1bed02f…`、镜像 `out/p21/fb-diag6/uefi/boot-p21-up1.img` `bb11e21b…`（82,579,456 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。

## 2026-10-30：fb-diag4 结果 + 代码审计（发现并修复诊断自身 bug）+ fb-diag5 已启动

- fb-diag4 日志 `20261030T083537Z`（boot `0x5143e37f`，73 轮）：进桌面 `frames=9`；触摸仍 `-30`。
- 结果行：`PMIC entries=244 sid0=12`，`PMIC sid0ppid 8 9 24 28 31 61`、`34 35 100061 39 3d 3e`，`PMIC page ppid=61 vset=00 en=01`，另有 0x39/0x3d/0x3e 被误当成页面。
- **审计发现（已确认的自身 bug）**：`cb_pmic_dump[8..19]` 存 SID0 PPID、页面结果又写 `cb_pmic_dump[16..23]`，两者重叠；页面扫描还从错误的槽位（4..15）取 PPID。因此 `0x100061` 是打包值被当成 PPID、`pages=1` 与那三行"页"都是伪数据。修复：新增独立 `cb_pmic_pages[8]`，页面扫描改为遍历真实 SID0 列表（提交 `89ce640` 之后的修复轮）。
- 修正后的可信结论：
  - arbiter v3 表共 **244** 项，SID0 只有 `0x08/0x09/0x24/0x28/0x31/0x34/0x35/0x39/0x3d/0x3e/0x61`（另 1 项被旧 bug 覆盖）；**没有 0x40–0x5f 的 LDO 页** → PM8998 regulator 页不归 AP 这个 EE，**从 AP 侧写 PMIC LDO 不可行**（推断，证据已很强）。
- 上游核对（`out/p22/reference/arch_arm64_boot_dts_qcom_msm8998-oneplus-common.dtsi`）：OnePlus 5 触摸节点为 `syna,rmi4-i2c@20`，`vdd-supply = vreg_l28_3p0`、`vio-supply = vreg_l6a_1p8`；`ts_reset_active` 与 `ts_int_active` 都是 gpio 功能、8 mA、**bias-pull-up**；`syna,reset-delay-ms=20`、`startup-delay-ms=20`。即复位低有效、空闲应被上拉到高；我们释放复位后仍读到低，与 `vio`（L6A 1.8 V）未上电一致。
- fb-diag5（本轮）：修复上述 PMIC 槽位重叠；并新增**三点电平采样** `TOUCH5 raw=.. gpiopull=.. muxed=..`——分别取固件原状、GPIO 输入 + SoC 上拉、切到 BLSP I2C 之后，用来排除"低电平是 QUP 造成的"。
- 候选 `out/p21/fb-diag5`：内核 `4a95f243…`、payload `cf41c9b6…`、固件 `f2af1b9d…`、镜像 `out/p21/fb-diag5/uefi/boot-p21-up1.img` `255dada9…`（82,579,456 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 若 `raw`/`gpiopull` 仍为 0：线路在固件阶段就是低的，供电缺失成立，下一步只能走 **RPM* 投票（AP 侧 PMIC 写放弃）；若 `gpiopull` 变高而 `muxed` 变低，则是 QUP 把线拉低，转向 QUP 配置。

## 2026-10-30：fb-diag3 结果——arbiter 计数来源错误；fb-diag4 已启动

- fb-diag3 日志 `20261030T081640Z`（boot `0x515f1c03`，69 轮）：进桌面 `frames=6`；触摸仍 `-30`。
- `PMIC core0=30000000 core1=3` + `PMIC map0 24b00 51400 51500 51600 800 900 27400 3c000`、`map8 20800 2400 2800 51e00 3100 6100 3400 3500`：
  - arbiter 版本是 **v3**，`core1&0x7ff=3` 不是 APID 数量（之前据此只扫 3 条，才得出"表里没有 0x45"的错误结论）；
  - 按 `entry>>8` 解码，前 16 项里 SID0 只有 `0x008(PON)/0x009/0x024/0x028/0x031/0x034/0x035/0x061`，**没有 0x045/0x05B**——说明 PM8998 的 regulator 页很可能不归 AP 这个 EE，而是 RPM 拥有，从 AP 侧写 PMIC LDO 这条路大概率不通。
- `TOUCH4 pullup sda=0 scl=0 rst=0 int=0`：内部上拉也拉不高（线被钳低）；但该采样点在复位仍为 0 时，需要排除复位态的影响。
- fb-diag4（提交 `89ce640`）修正两点并保留其他诊断：
  1. arbiter 改为**扫到第一个 0 表项为止**（上限 512），日志 `PMIC entries=.. sid0=.. pages=.. core0=.. core1=..` + 两行 `PMIC sid0ppid ...`，并对其中 0x40–0x7f 的页读 VSET/enable（`PMIC page ppid=.. vset=.. en=..`）；
  2. 把内部上拉实验**移到复位释放之后**（`rst=1`）再采样，排除复位态混淆。
- 候选 `out/p21/fb-diag4`：内核 `0c0c8235…`、payload `6253a84b…`、固件 `42eb3d8c…`、镜像 `out/p21/fb-diag4/uefi/boot-p21-up1.img` `e8423e29…`（82,583,552 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 若 sid0 列表仍无 regulator 页，则确认必须走 RPM 投票；若出现且 en=0，则可在 AP 侧写使能位。

## 2026-10-30：fb-diag2 结果——arbiter 解码本身是错的；fb-diag3 原始 dump + 上拉实验已启动

- fb-diag2 日志 `20261030T075820Z`（boot `0x47fe5973`，80 轮）：进桌面 `frames=8`；触摸仍 `error=-30 i2c=0/1`。
- 关键行：
  - `TOUCH2 gcc=1 rcg=0 mode=a5 status=c000000 op=c0 pins=4`——控制器使能后四线仍是 sda=0 scl=0 rst=1 int=0；
  - `TOUCH3 qup0=0 qup400=0 qup408=0 qupc=0 gcc52004=808a9d80 qupver=20070000`（QUP 未开始传输前读，0 属预期）；
  - `PMIC apids=3 revid=0 pages=0 err=2`——**arbiter 的 APID 表解码不成立**（真实表远不止 3 项、PMIC revision 也读不到），因此此前所有 PMIC 读/写（XNU 与 UEFI）都建立在错误地址上，"供电假设"仍未验证。
- fb-diag3（提交 `050f0df`）改为一次性记录原始数据与独立验证：
  1. `PMIC core0..7`：arbiter 版本/特性等原始寄存器；
  2. `PMIC map0/8`：映射表前 16 个原始 32 位表项（用于离线正确解码 PPID/APID）；
  3. `TOUCH4 pullup sda=.. scl=.. rst=.. int=.. after=..`：临时给 gpio87/88 打开 SoC 内部上拉再读取，随后恢复原配置——用来区分"外部上拉电源缺失（线会抬起）"与"未上电芯片把线钳在低位（仍为 0）"。
- 候选 `out/p21/fb-diag3`：内核 `e64d4e44…`、payload `cebefa07…`、固件 `fcf5f88c…`、镜像 `out/p21/fb-diag3/uefi/boot-p21-up1.img` `53b6e179…`（82,595,840 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。

## 2026-10-30：fb-lines 结果 + fb-diag2（一次性全链诊断）已启动一次

- fb-lines 日志 `20261030T073143Z`（boot `0x50006662`，108 轮）：进桌面 `frames=13`；触摸 `error=-30 i2c=0/1`；新增行 `TOUCH lines sda=0 scl=0 rst=1 int=0`——**复位已释放但 I2C 两线空闲为低**，与上拉所在的 `vcc_i2c_1v8`（pm8998_l6）未供电一致；`RAILS2 ppid4x5x=61/62/73/50/74/55/60/54`（列表被截断在 8 个，未覆盖整表）。
- 按用户要求改为**一次把触摸链路的日志加全**（提交 `21e1ed6`），本轮不再逐条追加：
  1. `TOUCH lines`：I2C 复用后、控制器使能前的 sda/scl/rst/int 电平；
  2. `TOUCH2`：GCC BLSP5 CBCR/RCG、QUP mode、控制器使能后的 QUP status/op、以及此时的四线电平；
  3. `TOUCH3`：QUP 0x0/0x400/0x408/0xC、GCC 0x52004、QUP 版本；
  4. `PMIC apids=.. revid=.. pages=.. err=..`：arbiter APID 总数、PMIC revision（PPID 0x001）、0x040–0x07f 段页面数与错误位；
  5. 每个 regulator 页一行 `PMIC page ppid=.. vset=.. en=.. err=..`（最多 12 页，读 +0x40/+0x46），用于直接识别 L6/L28 是否存在及是否已编程/使能。
  本轮仍然**只读**，不写任何 PMIC 寄存器。
- 候选 `out/p21/fb-diag2`：内核 `d1c5b5d2…`、payload `01ac7eda…`、固件 `3c0ce46b…`、镜像 `out/p21/fb-diag2/uefi/boot-p21-up1.img` `15a110f9…`（82,591,744 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 判读：`revid` 能确认 arbiter 读取路径与 PMIC 身份；若页面列表里出现 VSET≈0xE2（1.808 V）与对应 enable bit7，则 L6 实际已开、问题转向 QUP/时序；若全部 en=0 或页面缺失，则供电假设成立，下一步按真实页面写使能或走 RPM。

## 2026-10-30：fb-pmic 结果 + fb-lines（总线电平 / PPID 页）诊断已启动一次

- fb-pmic 日志 `20261030T070854Z`（boot `0x5133a1d8`，71 轮）：进入桌面（`frames=10`），触摸 `error=-30 i2c=0/1` 不变；`RAILS stage=1 l6 apid=-1 vset=00 en=00 l28 apid=146 vset=00 en=00 err=1`。
- 判读：我们假定的 PPID `0x045` 在 arbiter APID 表里**不存在**，`0x05B` 虽命中但 VSET/enable 读到 0，不能证明是 LDO28。由此推断上一轮 UEFI 的 rail 代码同样没有找到 L6、且 L28 因 VSET=0 而未写入——**PMIC 实际从未被写过**，"供电假设"仍未验证。
- 新增只读诊断（提交 `ca75a3e`）：
  1. `CheeseburgerTouch.cpp` 在 I2C 复用后、控制器使能前读取 gpio87/88/89/125 的输入电平，写入 `cb_touch[27]`，日志新增 `TOUCH lines sda=.. scl=.. rst=.. int=..`；
  2. `CheeseburgerButtons.cpp` 在 arbiter 线程里把 APID 表中 `0x040..0x07f` 的 PPID 收集到 `cb_rails[8..15]`，日志新增 `RAILS2 ppid4x5x=..`。
- 候选 `out/p21/fb-lines`：内核 `859faaf7…`、payload `2043a87f…`、固件 `29f54a5a…`、镜像 `out/p21/fb-lines/uefi/boot-p21-up1.img` `f9766c0e…`（82,587,648 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 判读方式：`TOUCH lines` 若 SDA/SCL 空闲为 0 → 上拉所在 IO 轨没电（供电问题）；若为 1 → 供电基本正常，转向 QUP 时序/地址。`RAILS2` 列出真实存在的 PPID 页，用于定位 PM8998 regulator 页后再考虑写入。

## 2026-10-30：fb-pmic（XNU 只读读取 L6/L28 状态）已启动一次

- 目的：前一轮 UEFI 侧开 L6/L28 后触摸仍是 `-30`，且 UEFI 打印未进持久日志；本轮在 XNU 内用**只读**方式读回两路 LDO 状态，直接把证据写进持久日志。
- 实现（提交 `b4197a7`）：`CheeseburgerButtons.cpp` 在既有 SPMI arbiter 线程里，按 PPID `0x045`/`0x05B` 找 APID，读 PMIC4 LDO 页 `+0x40`(VSET)/`+0x46`(enable)，结果存 `cb_rails[8]`；`BootLog.c` 新增 `RAILS stage=.. l6 apid=.. vset=.. en=.. l28 apid=.. vset=.. en=.. err=..` 行。**不做任何 PMIC 写。**
- 候选 `out/p21/fb-pmic`（基于 fb-rail，保留 UEFI 侧 rail 代码）：内核 `b9125cef…`、payload `bcba2d02…`、固件 `6bb562d9…`、镜像 `out/p21/fb-pmic/uefi/boot-p21-up1.img` `d2374f97…`（82,595,840 bytes）；pins 三项命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 判读方式：`RAILS` 行里 `en` 的 bit7 若为 1 而触摸仍 `-30`，说明供电已开但 NACK 另有原因；若为 0，说明 UEFI 写入没有生效或供电不在这两路；`err` 位标记 APID 未找到/映射失败/读失败。
- 关于“未进桌面是否与固件改动有关”：本次之前失败轮的签名与 10-26 R17 一致，且固件改动只涉及 PMIC 读/写、不触及显示路径；目前判断为该偶发分支，但需更多成功/失败对照才能定量。

## 2026-10-30：fb-rail 结果——落回已知黑屏分支，触摸 NACK 未变

- 回收 `out/p21/persistent-logs/20261030T064815Z/`：boot ID `0x4f96c356`，140 轮，写入错误 0。
- 显示：`DISPLAY clients=5 frames=0`、`SWAP check=0 rc=ffffffffffffffff`、`[fb events]` 只有 `s6 (0,0x80000003)` 与 `s50`，没有 `s4/s5`——与 `050326Z`/`052133Z` 相同的**首帧前黑屏分支**；本次与固件改动无因果证据（该分支同镜像会翻转，见前几条记录）。
- 触摸：与 fb-touch **逐项相同**：`error=-30 i2c=0/1`、`reset=1`、`hw=0x20070000`、`status=0x1321c4`、`op=0x90`。说明 UEFI 侧对 L6/L28 的读/写**没有改变触摸结果**：可能是写入未生效、RPM 覆盖，或供电不在 L6/L28。
- 缺口：UEFI 的 `UniXNU rails:` 行只出现在屏幕控制台，持久日志只含内核控制台尾部，未捕获；本轮不能判断 UEFI 代码是否找到 APID、是否写入成功。
- 下一步（尚未执行，建议）：在 XNU 板级运行时增加**只读** PMIC 探针——用与按键驱动相同的 arbiter 读路径，按 PPID `0x045`/`0x05B` 找 APID，读 `+0x40`(VSET)/`+0x46`(enable) 并写进持久日志；这样不依赖 UEFI 屏幕输出，可直接判断供电状态与 UEFI 写入是否生效。不做任何 PMIC 写。

## 2026-10-30：fb-rail（UEFI 打开 pm8998 L6/L28）已启动一次

- 目标：触摸在 `reset=1` 后第一次 I2C 传输被 NACK，判断为传感器未供电；按用户选择在 **UEFI 侧**打开 `pm8998_l6`（ldoa 6，1.808 V）与 `pm8998_l28`（ldoa 0x1c，3.008 V）。
- 实现（提交 `8e4f3f4`，回滚 tag `rollback-before-fb-rail` = `390a608`）：`rebuild/ipados-up1/fb-rail/UniXNURails.c` + `apply.py`，装进项目自有的 `UniXNUPkg/Driver/UniXNUDxe`，在 BDS 之前执行：
  - 通过 SPMI PMIC arbiter（core `0x0800f000`、chnls `0x08400000`、obsrvr `0x09400000`，来自设备 FDT）按 PPID `0x045`/`0x05B` 找 APID；
  - 先只读 VSET（`+0x40`）与 enable（`+0x46`）；仅当 VSET 非 0 且 bit7 为 0 时置 bit7，随后回读并打印 `UniXNU rails: ...`；
  - 命令格式依据上游 sheng 固件 SPMI 读实现与 Linux `spmi-pmic-arb.c` v2 `(opcode<<27)|((addr&0xff)<<4)|(bc&7)`；不读/写其它 PMIC 寄存器、不改电压。
- 候选 `out/p21/fb-rail`：内核/payload/ADT 与 fb-touch 完全相同（`96102686…` / `09c1e9b1…` / `775d411b…`），仅固件不同；FD `952c603f…`，镜像 `out/p21/fb-rail/uefi/boot-p21-up1.img` `87b07b0c…`（82,591,744 bytes）；pins 三项命中、门禁 PASS；FVMAIN 内确认含 `UniXNU rails` 字符串；目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区。
- 验收：若供电确实缺失，下一轮 `TOUCH error` 应从 `-30` 变为 0 或更后面的阶段（F01/F12 出现）；若仍是 `-30`，说明供电不在 L6/L28 或写入未生效，需读回 `UniXNU rails` 控制台行判断。

## 2026-10-30：fb-touch 进桌面正常，触摸停在 I2C NACK（供电）

- 回收 `out/p21/persistent-logs/20261030T061244Z/`：boot ID `0x4feb4fc9`，86 轮，写入错误 0；`DISPLAY frames=14`、`SWAP check=14 transfer=14 rc=0`，用户确认进入桌面；`[fb events]` 与 fb-diag 相同（`s8 out2=438/780` → t≈377 s `s4/s5` 成对），说明触摸改动没有改变显示分支。
- 触摸进度（已确认）：`reset=1`（GPIO89 回读高，复位已释放）、`hw=0x20070000`（QUP v2）、`clock=0`（GCC XO 配置检查通过）、`gpio=0/e2`、`status=0x1321c4`（含 bit8，NACK）、`op=0x90`；`error=-30 i2c=0/1`——第一次 I2C 传输被从机 NACK。
- 判读：地址 0x20 在 Recovery 2.4 s 能正常探测（`out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`，固件 ID `0xeb101035`），所以不是地址/接线/复位问题，最可能是**触摸供电未开启**。
- 供电事实（设备自身 FDT）：`pm8998_l6`、`pm8998_l28` 是 **RPM-SMD regulator**（`qcom,rpm-smd`，`resource-name=ldoa`，`resource-id=6 / 0x1c`，1.808 V / 3.008 V，`qcom,set=3`），不是可直接用 SPMI 打开的 LDO 节点；当前 fastboot → UEFI → XNU 路径没有任何 RPM 投票。
- 下一步（三选一，均需新候选与门禁）：
  1. **UEFI 侧**在 ExitBootServices 前用固件自带的 PmicDxe/SPMIDxe 打开 LDO6/LDO28（改动最小；本机固件含这两个 DXE）；
  2. **XNU 侧**直接写 PM8998 LDO 寄存器（需要 PMIC4 寄存器表；RPM 可能覆盖，风险较高，不推荐先做）；
  3. 实现 RPM-SMD 请求（最正统，工作量最大）。
- 验收：`TOUCH i2c` 计数增加且 `error` 不再是 `-30`、F01/F12 出现，随后 HID 安装；触摸方向仍待实机核对。

## 2026-10-30：fb-touch（成功版 + S3320 复位修复）已启动一次

- 在实机验证可进入桌面的 `fb-diag` 基础上同步触摸修复（`CheeseburgerTouch.cpp`：GPIO89 mux 0、输出电平写 IO bit 1，提交 `b42b68a`），其余源码、ADT（`775d411b…`）、启动参数（644 字节）与 `fb-diag` 逐字一致。
- 候选 `out/p21/fb-touch`：内核编译 `6507872e…` → 预链接 → panic-screen 后 `96102686…`；payload `09c1e9b1…`；固件 `486db499…`；镜像 `out/p21/fb-touch/uefi/boot-p21-up1.img` `30345d4f…`（82,591,744 bytes）；pins 三项命中、门禁 PASS。目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 预期：若 `TOUCH error` 从 `-54` 变为 `-30`/`i2c>0`，说明复位修复在这条基线上同样生效、且停在触摸芯片未上电（pm8998 l6/l28 未在 iOS 运行时开启）。同时验证触摸改动不会改变显示分支（`[fb events]` 仍应出现 `s8 out=438/780` 与 `s4/s5` 成对）。
- 观测窗口：成功轮首个 `sel 4` 在 t≈370 s，建议跑满 ≥600 s。
- 已推送 `main` 至 `origin`（`35936aa..1d6cd0e` 之后的本轮提交以本地为准）。

## 2026-10-30：fb-diag 成功进入桌面（14 帧），事件环拿到首个成功样本

- 回收 `out/p21/persistent-logs/20261030T055325Z/`：boot ID `0x50c0cf75`，79 轮，写入错误 0；`DISPLAY clients=57 frames=14`、`SWAP check=14 transfer=14 rc=0`、`EXITS 696`；用户报告**成功进入桌面**。
- `[fb events]` 首个完整成功序列（`cntpct/cntfrq` 秒）：
  - `E00 t=182.17 p94 s8 out2=438/780 rc=0`（尺寸查询返回 1080×1920）
  - `E01 t=182.22 p56 s15 in2=0/10 out1=0 rc=e00002bc`、`E02 t=182.22 p56 s8 out2=438/780 rc=0`
  - `E03 t=213.56 p56 s6 in3=0/80000003/0 rc=0`、`E04 s50 in2=10000/0 rc=e00002c7`
  - 静默约 157 s 后 `E05 t=370.65 p56 s4 out1=1 rc=0`、`E06 s5 rc=0`，随后 `s6 (n,1)/(n,80000000)` + `s4` + `s5` 成组重复，`s5` 每次 rc=0，14 帧与 14 次 `s4/s5` 对应。
- 结论（已确认）：selector 8 返回正确尺寸；失败分支缺的不是尺寸或 selector 50，而是 `s6` 之后的配置与 `s4/s5` 提交。失败轮在 `sel 6` 首次查询后停下，成功轮在 t≈370 s 才出现首个 `s4`。下一轮可在失败日志里对照 `[fb events]` 缺失的位置定位等待点。
- 本轮无源码改动，仅诊断候选 `out/p21/fb-diag`（镜像 `bf6d86d7…`、内核 `29d3a758…`）被实机验证可进入桌面；该镜像作为新的对照基线保留。

## 2026-10-30：fb-diag 诊断候选（selector 事件环）已启动一次

- 依据 `[mfb clients]` 全量行与 `[display state]` 得到的新分界：成功轮 backboardd 的 `sel 6` 完成 `(n,0x80000001,0) → 0` 序列，把显示对象 `[5820]/[5824]` 累加到 `0xe`（d2）/`0x2b`（window2g），`(n,0x80000000,0)` 返回 `0xe00002d5`(Busy)，随后 `sel 4`/`sel 5` 各 14 次成功；失败轮 `sel 6` 只调用一次 `(0,0x80000003,0) → 0`，没有 4/5，`[display state]` 的 `5816/5820/5824/5828` 全 0，`[display]` 的 `Current Display` 为空。剩余未知量是 `sel 6`/`sel 8` 的 scalarOutput。
- 上游依据：sheng `package/MobileDisplayProviderProbe.cpp` 用 `selector 8 + scalarOutputCount=2` 读宽高，证明 selector 8 是尺寸查询；本机 `LegacyDisplayProbe.cpp` 的客户端拦截与 sheng 同源。
- 新增只读诊断（提交 `a6651e8`）：32 槽事件环记录 selector 4/5/6/8/15/50 的时间（cntpct/cntfrq）、pid、客户端代际、至多 4 个 scalarInput/scalarOutput、原始返回与丢弃数；报告新增 `[fb events]` 段（≤2560 字节）。不改变任何返回值，不新增睡眠/分配/锁。
- 候选 `out/p21/fb-diag`（自 d2 复制，ADT `775d411b…`、参数与 d2 逐字相同）：内核编译 `466280ad…` → 预链接 `7b19d6ab…` → panic-screen `29d3a758…`（125,698,048 bytes）；payload `b44f0ad3…`；固件 `d131c854…`；镜像 `out/p21/fb-diag/uefi/boot-p21-up1.img` `bf6d86d7…`（82,567,168 bytes）；pins 三项命中、门禁 PASS（644 字节）。目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 观测窗口：历史首帧 307/318/524 s，建议跑满 ≥600 s。回收后比较 `[fb events]` 里 `sel 8` 与首个 `sel 6` 的 out 值；若与成功轮相同，则排除“provider 返回不同”，转向通知/等待路径。

## 2026-10-30：黑屏分析勘误与旧镜像复跑

- 用户回到 Recovery 后回收 `out/p21/persistent-logs/20261030T052133Z/`，boot ID `0x4bd8fea6`、962 秒、192 轮、写入错误 0；最新已记录的启动是 touch-window2g `boot-once2.log`（Fastboot 接受；日志本身不证明镜像哈希）。本轮 `stage=10 clients=5 frames=0`、SWAP/prepare 均 0；backboardd 的 15 次调用中 selector 6/8/88 成功，12 等多个 selector 返回 `e00002c7`，15 返回 `e00002bc`。
- **更正下方及报告的 NotOpen 判断**：同版本 XNU `iokit/IOKit/IOReturn.h` 定义 `0xe00002c7` 为 `kIOReturnUnsupported`，`kIOReturnNotOpen` 实际是 `0xe00002cd`。不能由 `e00002c7` 判断 open 失败或发布时序竞态。
- 更关键：成功的 d2 日志 `20261029T172431Z` 的 backboardd selector 50 也返回 `e00002c7`（2/2 失败），同时 selector 4/5 均成功 14 次并显示 14 帧；所以 selector 50 的 unsupported 不是黑屏特有签名，更不能单独作为根因。上游 `.work/p21-official-framework/scripts/check-system-framebuffer-client.py` 也将其原版 frame-info 查询的 `e00002c7` 作为 unsupported 记录，但未证明本机 selector 50 的语义。
- 新旧黑屏对照仍支持问题发生在观察到的首帧提交之前；`stage=10`、`newUserClient` 成功和显示服务匹配不证明所有显示/用户态状态相同。采样仅最多保留 8 个客户端，不能从 0/0 推断进程从未通过其他对象调用。触摸、身份与时序均未被全面排除。下一步应分析 backboardd 为什么在初始查询后没有进入 selector 4/5，优先符号化已有线程栈、关联客户端调用与用户态依赖状态；不应盲改 open 返回码或屏蔽服务。设备已回到 Recovery，未重新启动或写入分区。

## 2026-10-30：黑屏分支的日志分析（framebuffer 客户端 open 失败）

- 失败轮的共同签名：某个显示客户端（backboardd / `IOMFB_bics_dae` / `IOMFB_FDR_Load`）最后一次外部方法调用是 **selector 50，返回 `0xe00002c7`（kIOReturnNotOpen）**，且 backboardd 的帧调用为 `in/out=0/0` 或极少；成功轮 backboardd 最后是 **selector 5（swap_submit）rc=0**，`in/out=77/77` 或 `194/194`。失败轮 `prepare calls=0`、`SWAP rc=ffffffffffffffff`，即一次帧都没提交。
- 内核侧显示发布两轮一致（`IOMobileGraphicsFamily` 注册时间 74 s vs 66 s、`NativeLegacyDisplay` 注册、`DISPLAY stage=10 meta=0 reg=1`），所以差异不在内核显示初始化，而在用户态客户端连接时序。
- 用户态侧差异：失败轮进程退出 599/618/678 次、SpringBoard 8 线程；成功轮 1088 次、17 线程。失败轮 msgbuf 里 datamigrator/语言服务/bluetoothd 反复重启。首帧历史：307 s、318 s、524 s；失败轮 881/977 s 仍为 0。
- 上游核对：官方 `docs/reference/troubleshooting.md` 明确写“Firmware stalls → Random Silicium stall → Start the same image again”，承认随机卡住需重试；官方 sheng（同为 iPadOS 17.7.11/21H461、iPad7,1）`board.toml` 与本机的差异：`offline-legacy-unit-scale=2`（本机 1）、`offline-mobile-graphics-client=1`、`sheng-disable-jobs=com.apple.identityservicesd,com.apple.locationd,com.apple.CommCenter,com.apple.bluetoothd`、`boot-ui-focus3/4`（仅诊断用途）、`debug=0x14e`、`kt_fixed_zones=120`、`-apfs_shared_datavolume`。上游没有“NotOpen 黑屏”的直接案例，也没有可直接合并的修复。
- 待验证的下一步：给 `wrappedExternal` 增加 selector 50 与 open/close 的一次性记录（pid/时间/rc/当时的 `native_legacy_stage`），确认失败轮是否发生在显示服务发布之前；若成立，再考虑让 provider 更早发布或让 open 路径返回可重试状态，而不是直接失败。

## 2026-10-30：同一张 d2 复现失败——镜像代码不是变量

- 回收 d2 复跑日志 `out/p21/persistent-logs/20261030T050326Z/`（boot ID `0x50e2b156`，176 轮，写入错误 0）。同一张 `1999371c…` 镜像：首次 `20261029T172431Z` 为 `clients=115 frames=14`、`EXITS 1088`、backboardd 打开 FB `77/77`；复跑为 `clients=5 frames=0`、`EXITS 599`、backboardd FB `0/0`，`FB0 IOMFB_FDR_Load 15/15 enter/leave=50/50 rc=e00002c7`（kIOReturnNotOpen）、`FB2 MobileGestaltH rc=e00002f0`。
- 因此“触摸改动导致黑屏”与“手机身份导致 frames=0”两个判断都被证伪：镜像、payload、ADT、参数都相同的 d2 会两种结果都出现。触摸代码只影响触摸。
- 时间线：三次出画面的 run 首帧分别在 t≈307 s（`164350Z`）、t≈318 s（`172431Z`）、t≈524 s（`161111Z`）；两次黑屏 run 等到 881 s / 977 s 仍 `frames=0`、`prepare calls=0`、`SWAP rc=ffffffffffffffff`（从未提交）。failure 端 `IOMobileFramebuffer` 客户端反复拿到 `kIOReturnNotOpen`，表明用户态与 framebuffer 的握手在按启动时序分支，而不是在像素路径上失败。
- 三份 msgbuf 对比：APFS 数据卷都完成挂载（disk0/disk1 命名不同）、kext 加载序列一致、SpringBoard 在两种结果下都以约 1390 次/秒 CPU 唤醒自旋；黑屏 run 只有 8 个线程、正常 run 17 个。
- 按用户要求再往前启动 `out/p21/touch-window2g/uefi/boot-p21-up1.img`（`29029186…`，iPad7,1，上次 39 帧），`out/p21/touch-window2g/boot-once2.log` Sending/Booting OKAY；观察窗口按首帧历史（≥300 s）放宽到 ≥480 s。

## 2026-10-30：touch-fix2 回滚到 d2，全链差异核对

- 用户选回 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img`（`1999371c…`）并已临时启动（`out/p21/phone-ui-d2/boot-once2.log`，Sending/Booting OKAY）。此前 touch-fix2 轮日志由监视器回收：`out/p21/persistent-logs/20261030T044653Z/`，boot ID `0x4bfec417`，216 轮，写入错误 0。
- touch-fix2 触摸确有推进：`error=-58` → `error=-30`、`i2c=0/1`、`hw/clock` 已过；说明 mux 0 + 输出 bit1 的复位修复生效，I2C 已发起第一次传输但在 `cb_discover` 首读失败（`SynapticsRMI4.h` 返回 `-30`）。触摸芯片此时仍未上电（pm8998_l6 1.808 V / l28 3.008 V 在 iOS 运行时未确认开启），下一步应先在板级开启这两路供电再谈 HID。
- 显示仍未出帧：`DISPLAY clients=5 frames=0`、`SWAP check=0`、`prepare calls=0`；用户报告黑屏。
- **全链对比（d2 可用版 vs touch-fix2 黑屏版）**：
  - payload 内 `unixnu.toml`、`kernel.macho.adt`、`kernel.macho.trustcache`、`scratch-data.img` 哈希完全一致；启动参数 644 字节逐字一致；仅 `kernel.macho` 不同（`fe6be784…` vs `031cf674…`）。
  - 板级运行时源码 `package/BoardRuntime` 只差 `CheeseburgerTouch.cpp` 一个文件；显示映射（`kIOMapInhibitCache`）、`BootLog.c`、按键代码完全相同。
  - 构建方式同为“官方 framework kernel.build → `mach.development.bcm2837.unstripped.noctf` 预链接 → panic-screen → payload → 独立 overlay 全量固件 → pins → pack → 门禁”；仅 overlay 名不同（`unixnu-up1-phone-d2` vs `unixnu-up1-touch-fix2`）。内核 `__text` 尺寸 d2 `0x7fb534`、touch-fix2 `0x7fb564`，差 0x30，与只改触摸函数一致。
- 结论：**此前“手机身份导致 frames=0”的判断撤回**——d2 同为 iPhone10,3/D22 且有 14 帧。现有证据只把差异收敛到触摸代码；但黑屏轮的用户态签名与触摸改动之前的 `20261029T165823Z` 相同（clients=5、frames=0、FB2 15/15 rc=e00002c7、FB3 MobileGestaltH），且黑屏轮 msgbuf 显示 datamigrator/语言服务/蓝牙在持续运行与重启，说明用户态在推进但没有提交帧，不能仅凭日志断言触摸驱动直接导致黑屏。
- 下一步建议：显示工作以 d2 为基线；触摸改动以启动参数开关隔离（默认关闭），避免再次影响显示；开启前先补 pm8998 l6/l28 供电。

## 2026-10-30：touch-fix2（GPIO89 mux/输出位）与黑屏记录，回滚待选

- 用户反馈 touch-drive（`db1385a3…`）黑屏、进不了系统。回收 `out/p21/persistent-logs/20261030T040140Z/`：boot ID `0x4f5bb00c`，927 秒 / 185 轮 / 写入错误 0；`DISPLAY clients=5 frames=0`、`prepare calls=0`、`SWAP rc=ffffffffffffffff`、`TOUCH error=-58`。
- 该签名与**触摸改动之前**的手机身份首启 `20261029T165823Z` 逐项一致（clients=5、frames=0、`FB2 backboardd 15/15 enter/leave=50/50 rc=e00002c7`、`FB3 MobileGestaltH`、`FB0` 独占）；因此现有日志不能单独证明黑屏由触摸改动造成，此点保持未定论记录。
- 触摸根因（设备 FDT `ts_reset_active` + Linux pinctrl-msm）：gpio89 的复用表只有 `gpio/tsif0/phase_flag`，复位必须 mux 0，原代码写的是 mux 3（I2C 的 mux 3 只属于 gpio87/88）；IO 寄存器 bit1 才是输出电平、bit0 是输入回读，原代码写 bit0，pad 实际从未拉高，故 `-58`。
- 修复提交 `b42b68a`。候选 `out/p21/touch-fix2`（自 touch-drive 复制，ADT 保持手机身份 `775d411b…`）：内核 `031cf674…`、payload `3ac2df84…`、固件 `66868a03…`、镜像 `out/p21/touch-fix2/uefi/boot-p21-up1.img` `80c8b19e…`（82,583,552 bytes）；pins 三项在 P9Probe/DarwinBoot/P4RamPayload 命中，门禁 PASS（644 字节参数），目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区。
- 实机结果：用户报告仍未进入系统界面。该修复源码仅保留作比较，不进入回滚候选。

### 可回滚的“最近一次实机确认能进系统”候选（待用户选择）

1. `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` — SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`（01:13）；手机身份 iPhone10,3/D22 + `/buttons`（hold/voldown/volup、home-button-type=2、thin-bezel=1）+ `BootLog.c` DISPLAY SCALE 诊断；用户确认进入系统画面（平板布局、触摸无效），日志 `20261029T172431Z` 有 14 帧。
2. `out/p21/touch-window2g/uefi/boot-p21-up1.img` — SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`（00:33）；iPad7,1 身份 + P23 高内存窗口（`darwinboot-memory-range=0x100000000+0xbd8c0000`、`PcdSystemMemorySize=0x13D8C0000`）；用户确认显示原始 iPadOS 界面，日志 `20261029T164350Z` 有 39 帧。

## 2026-10-30：iPadOS 切换为 main

- `main` 合并保留原 iOS 分支历史，合并后的代码树以 `ipados-17.7.11` 的 `4dc3167` 为准；原 iOS 工作保留在 `ios-16.7.16`。README 调整主线说明及组件状态，硬件源码、参数与镜像未因此更改。
- 切换前 Git 完整 bundle 和未跟踪文件副本保存在 `out/repository-backups/20261030-before-ipados-main/`，bundle 校验通过；不包含被忽略的系统镜像和生成物，它们仍在原位置。设备和构建状态以其后记录为准。

## 2026-10-30：touch-drive 修正重建（复位脉冲 + 构建链修复）

- 触摸：GPIO89 改为厂商复位序列——mux 0、上拉、8 mA，先写锁存低→置 OE→2 ms→锁存高→50 ms→回读；仍低返回 `-58`。
- 构建根因：resolved 树的 `kits/cheeseburger` 软链接原先指向 **d2 的 package**，所以候选树里的触摸改动从未进入内核；`p21-build-board-runtime.py` 增加 `P21_BOARD_ROOT` 覆盖，驱动把它指向候选的 `source-tree`，并把 `kits/cheeseburger` 重指到候选 `package`。链接阶段的 build-report 源码哈希校验因此通过。
- 重新整链：内核 `6a01d1f2`（编译）→ unstripped 预链接 → panic-screen 后 `bbbb2099`；payload `0d4f0932`；固件 `f4c98f7e`（独立 overlay，增量）；三项 pins 通过；镜像 `db1385a3`，门禁 PASS（644 字节）。
- 尚未启动。

## 2026-10-30：touch-drive 候选（S3320 复位拉高）

- 修复：`package/BoardRuntime/CheeseburgerTouch.cpp` 在输入上拉仍读到低电平时，改为置 OE、驱动 GPIO89 高（ACTIVE_LOW 复位释放）并回读；仍低则回退并返回 `-58`（提交 `5f299f0`）。
- 构建（按本仓库流程）：候选树 `out/p21/touch-drive/source-tree`（自 d2 复制）内跑官方框架 `kernel.build`（`scripts/p21-compile-touch-drive.py`，内核 `db6e18f4…` 编译通过）→ 取 `mach.development.bcm2837.unstripped.noctf` 预链接（`d440a90c…`）→ `patch-xnu-entry-probe.py --panic-screen --no-reset`（最终内核 `9efd466e…`）。
- payload `571e414a…`；固件在**独立新 overlay** `/home/dinnerabc.guest/unixnu-up1-touch-drive`（lower 叠加 d2 及以下历史层）全量重编，FD `9d2fce81…`，三项 pins 在 P9Probe/DarwinBoot/P4RamPayload 中核对通过；镜像 `out/p21/touch-drive/uefi/boot-p21-up1.img` `55c694fb…`（82,595,840 bytes），P21 门禁 PASS（644 字节参数）。
- 尚未启动。下一轮重点：日志 `TOUCH error` 是否不再是 `-54`（期望 0 或 `-58`）、`reset=` 值、I2C/F01/F12/HID 是否开始计数；显示与 scale 应与 d2 相同（frames>0）。

## 2026-10-30：手机布局上游对照与本轮日志

- 设备回到 Recovery 后回收 `out/p21/persistent-logs/20261029T172431Z/`：新 boot ID `0x5a0ea281`，511 秒、102 次记录、写入错误 0；用户报告已进入系统但仍为平板布局、触摸不可用。
- 实际 `DISPLAY SCALE status=0 calls=1 chosen=1`，`frames=14`、`SWAP check=14 reject=0 transfer=14`；日志记录 `1242x2208 -> 1080x1920`。这是帧像素尺寸与运行时发布的属性，不是对 UIKit 实际 idiom 或逻辑尺寸的直接测量。触摸仍 error=-54、I2C=0/0。
- 官方 sheng 配置 `offline-legacy-unit-scale=2`，但为 iPadOS 平板配置；ios_sagit 使用 iPadOS、声明已到桌面，现有文字不足以确认其实际布局和 scale；vphone-cli 的 iPadOS 文档说明设备身份与 display-scale 共同影响呈现，不能把其虚拟设备参数直接当成本机配置。当前 phone 身份搭配 iPadOS 根和 chosen scale=1 均为需要进一步核对的差异，尚无证据证明任一项是唯一原因。
- 下一步优先读取实际 MobileGestalt idiom、有效 UIScreen scale 和逻辑 bounds，并对照同版本可复用的手机布局案例；scale=2 可以作为独立对照实验，不能当成已验证的手机布局修复。未经定位不清除 Data。设备保持 Recovery，本轮未启动或写入分区。

## 2026-10-30：手机身份诊断版画面反馈

- 用户随后报告 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` 已进入系统画面。用户进一步确认布局仍是平板样式且触摸不可用；按键是否可用尚无反馈，本轮新 boot ID 日志尚未回收。设备的实际连接状态应在下次操作时重新确认。

## 2026-10-30：后续开发交接

- 项目简介按官方设备仓库结构更新，后续 DeepSeek 接手入口为 `docs/DEEPSEEK_HANDOFF.md`，包含当前候选、已确认结果、日志和构建路径、手机界面、触摸、按键及启动限制。
- 交接时系统 ADB 服务为 `tcp:localhost:5037`，目标未在 ADB 或 Fastboot 枚举；手机实际画面未知。`phone-ui-d2` 本轮仍待用户反馈及新日志，不应以 Fastboot 成功推断进入系统。

## 2026-10-30：手机身份按键及显示 scale 诊断

- 基于 phone-ui 的 ADT 补充 `/buttons` 的 hold/voldown/volup、home-button-type=2 和 product thin-bezel=1，保留一加 5 已有电源/音量和双音量模拟 Home 处理；尚未验证物理交互或混合 iPadOS 根是否支持手机布局。
- `BootLog.c` 新增实际 `DISPLAY SCALE status/calls/chosen/rotation` 快照，其他显示、SWAP、线程栈和触摸诊断保留。固定官方设备代码会在 phase4 时按 `offline-legacy-unit-scale=1` 发布 chosen display-scale；静态 ADT 中缺失不代表运行时缺失。
- `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` SHA256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`；重编内核、载荷、固件，编译时 pins、FV 模块与离线门禁通过，目标设备 3b25cc7c 临时 boot 一次 Sending/Booting OKAY，实际画面待用户观察；未写分区。上一轮日志已回收。
- Data 种子本地只读挂载被 hdiutil 拒绝，未确认缓存是否存在，未清理缓存或手机 Data；用户允许必要时清理，应在定位明确后限定目标操作。用户打算延长等待，返回 Recovery 后回收新 boot ID 日志并检查 DISPLAY SCALE / SWAP / 栈。触摸 error=-54 仍未修复。源码提交 `5a03785`、`7c4dfe6`。

## 2026-10-30：手机身份与官方规范复核

- 固定 UniXNU `16e592abee8a550dec8154ed1f38b5ebc4f16213` 的 `docs/guide/mobilegestalt.md` 要求 iPadOS 根配 pad、iOS 根配 phone；当前手机候选为用户要求的 phone 身份 + iPadOS 根实验，与该推荐不一致，不能据此断言成功或失败的具体原因。
- 实际 `out/p21/phone-ui/adt/kernel.macho.adt`：model iPhone10,3、target D22、subtype D22AP、product iPhone X、idiom phone、artwork subtype 2436、scale 3、fallback iPhone9,2；这些字段与本地 P23 ADT 一致，root compatible 保持 `device-tree\0qcom,msm8998\0`。候选无 `/buttons` 节点、无 `home-button-type` 和 chosen `display-scale`；官方全面屏示例需要无 menu 且 home-button-type=2，实际一加 5 物理按键不能直接套无 Home 机型。P23 有 buttons 但无 home-button-type。手机候选未清 MobileGestalt 缓存，实际系统读取值尚未验证。
- 上游 main 当前 iPadOS profile 增加了 product-id；本机固定版本及两个候选均没有。该值为 J120 特定，不能用作 iPhone 值。实际日志 579s SWAP=0/frames=0，未证明上述任何差异为根因；优先保持既有日志，查实际 MobileGestalt、用户栈和显示提交之前的等待。

## 2026-10-30：手机身份候选日志

- 用户反馈手机身份候选未出现系统画面。Recovery 回收 `out/p21/persistent-logs/20261029T165823Z/`，新 boot ID `0x4dd45577`，579 秒、116 记录、写入错误 0。`SpringBoard p27`、`backboardd p56` 在运行，八核在线，VM managed=2048MiB；显示 clients=5，帧、SWAP 检查及传输均为 0。UI 重点进程无已捕获致命退出。
- 相比之前 iPad 身份实机 39 帧，本轮未观察到任何提交，现有证据不足以归因屏幕缩放或判定 identity 为唯一原因；应分析用户态提交前等待、身份匹配及缓存，并保留日志里的线程栈。触摸仍 -54/reset=0/I2C=0，无新触摸修复。
- 设备当前 Recovery，未进行新启动；保留原先 `out/p21/touch-window2g` 可进入系统的候选。

## 2026-10-30：手机界面身份实验与触摸日志

- 用户确认 `out/p21/touch-window2g` 进入系统但触摸无响应。回收日志 `out/p21/persistent-logs/20261029T164350Z/`，boot ID `0x5c58198d`、94 rounds、0 写入错误；`online=8`，39 帧；触摸 `error=-54 reset=0 i2c=0/0`，F01/F12/HID 均未启动，说明上拉不足，尚未证明原因是电源还是复位线被拉低。Recovery 只读检查显示 pm8998_l6 1.808 V 和 pm8998_l28 3.008 V enabled；这仅证明 Recovery 状态。原厂 Recovery dmesg 保存在 `out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`。
- 用户授权在原 iPadOS 上尝试手机界面：复制 P23 iPhone10,3/D22 的 model 和 product identity，保留 OnePlus compatible、原硬件按键、iPadOS root、8 CPU、2 GiB 和触摸内核；没有修改 Data 或清缓存。官方建议身份与系统系列一致，这次为有风险的界面实验，效果未证实。
- `out/p21/phone-ui/uefi/boot-p21-up1.img` SHA-256 `53b8f0dfd837f3d1903dbde20fe6fd3e6f9186097681194c8676799a0cac452d`；payload/UEFI 重编、pins 和离线门禁通过；目标设备临时 Fastboot boot 一次 Sending/Booting OKAY，运行结果待观察，无分区写入。源码 `rebuild/ipados-up1/phone-ui/` 提交 `e59a5a2`。

## 2026-10-30：恢复 P23 高内存窗口

- 用户报告上一触摸/8 核/2 GiB 候选疑似 `NO WINDOW`。Recovery 窗口 `out/p21/persistent-logs/20261029T163303Z/` 与前轮相同，boot ID `0x55da2956`，未出现本轮记录；这不能单独证明内核未执行。
- P23 使用 `darwinboot-memory-range=0x100000000+0xbd8c0000`、`PcdSystemMemorySize=0x13d8c0000` 及 DarwinBoot 范围分配支持；前版 iPadOS 只带了 2 GiB size，未带范围和固件支持。本机 FDT RAM bank 证据见 `docs/P4_ADT_BRINGUP.md`。恢复 P23 局部补丁，并强化 AllocateAnyPages 的范围与 2 MiB 对齐检查；仍只分配 UEFI 报告的可用页。范围 host test 通过，实机成功未证实。
- 候选 `out/p21/touch-window2g/uefi/boot-p21-up1.img` SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`，沿用触摸及 8 核内核，重新打包 ADT、payload、固件并通过离线门禁。目标 `3b25cc7c` 仅临时 Fastboot boot 一次，Sending/Booting OKAY，未写分区；屏幕及 2 GiB/触摸结果待用户确认。
- 修复源码、host test 与边界说明见 `rebuild/ipados-up1/window-fix/`，提交 `949ce25`。Recovery 返回后回收新日志，不自动重复启动。

## 2026-10-30：UP1 触摸、八核和 2 GiB 候选

- 用户确认上一版 `out/p21/up1-clean-rebuild/uefi/boot-p21-up1.img` 已进入 iPadOS。其新日志 `out/p21/persistent-logs/20261029T161111Z/` boot ID `0x55da2956`，268 rounds、写入错误 0。手机端持久日志已工作；主机 `scripts/unattended/monitor.py` 未运行，读取并非自动执行。
- 按一加 5 上游设备树的 GPIO89 input pull-up，修复 S3320 reset 输入未配置上拉导致的潜在初始化失败；不改变电源或驱动复位脚，低电平仍报告 -54。P22 的 R6 曾记录 -54、未发生 I2C；触摸是否修复尚待本轮实机结果。宿主 RMI4 测试通过。
- 新候选 `out/p21/touch-up1/uefi/boot-p21-up1.img` SHA-256 `61562019dd4c8f523c6c17664ea6f68b99bfca0271eac86f62998cde55b8391d`，基于 UP1，仅导入 P22 触摸与 SMP 修改；ADT 的 CPU/PSCI/内存/vnode 来源为本地 iOS Stage C，8 核、2 GiB、33792 vnode。历史 2 GiB iPadOS 尝试出现过 NO WINDOW，本轮不预设成功。
- 本轮内核、payload、UEFI 已构建，EFI 编译 pins 与 FV 模块匹配，`out/p21/touch-up1/offline-gate.json` 通过。设备 `3b25cc7c` 临时 Fastboot boot 一次，Sending/Booting OKAY，未写分区；设备实际运行状态待用户报告。若 NO WINDOW，在内核之前失败，不能判断触摸；若进入系统请测试点击/滑动，回 Recovery 后读取新的日志。
- 本次源码和流程提交 `166b938`；构建与运行日志在 `out/p21/touch-up1/`，构建脚本 `rebuild/ipados-up1/touch-scripts/`，VM overlay lower 仍需要保留。

## 2026-10-29：iPadOS UP1 独立重建与临时启动

- 用户报告此前 `out/p21/upstream-a/uefi/boot-p21-up1.img` 已进入 iPadOS 界面。该轮 Recovery 持久日志位于 `out/p21/persistent-logs/20261029T153842Z/`，boot ID `0x59359b02`，190 次记录，写入错误为 0；先读回再启动新镜像。
- 新内核从提交 `f89c28c9f83ec171ba7ee43018b0cd20766d9698` 导出的独立源码目录 `out/p21/up1-clean-rebuild/source/` 编译；官方框架和参考源码使用本地固定输入。日志在 `out/p21/up1-clean-rebuild/kernel-build.log` 及其独立构建树的 `logs/`；预链接、panic 屏幕与 payload 构建日志也留在该目录。
- UEFI 使用 Linux VM 的 `/home/dinnerabc.guest/unixnu-up1-clean/source` 和空构建输出；源码 overlay 的 lower 层仍依赖原有 UP1 及此前本地 VM 层，不是独立下载的新上游源码。构建脚本保存在 `rebuild/ipados-up1/clean-scripts/`，VM 中的 overlay 层必须保留。
- `out/p21/up1-clean-rebuild/uefi/boot-p21-up1.img` SHA-256 `7aabe63b44d37972658de89ca934151d847d2e6a8d817b45527f87382a00b772`；载荷、内核、ADT 校验值在编译后的 EFI 模块中匹配，三项 EFI 模块与 `FVMAIN.Fv` 对应节一致；镜像内容往返校验通过。注意 FVMAIN 在 FD 中经封装，本轮未单独反向解包 FD 验证 FV 内容。
- 设备 `3b25cc7c` 已通过 `fastboot boot` 临时启动此新镜像一次，Sending 和 Booting 均 OKAY；实机是否出现画面尚待用户报告。未写任何分区；boot 输出在 `out/p21/up1-clean-rebuild/boot-once.log`。出现问题时先记录画面，返回 Recovery 后回收新 boot ID 日志，勿重复自动启动。

## 2026-10-29：恢复 iPadOS userdata

- 用户要求启动一次：已核对 `out/p21/upstream-a/uefi/boot-p21-up1.img` SHA256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`，P21 离线门禁 PASS（`pins_log_checked=false`），设备 `3b25cc7c` 通过 `fastboot boot` 临时启动，Sending/Booting 均 OKAY；未修改分区或预置 misc。本次设备运行画面尚待用户观察，不能以传输成功当作 iPadOS 启动成功。回 Recovery 后读取日志再进行后续启动。

- 当前分支 `ipados-17.7.11`，HEAD `08543c9`；未重置工作区，保留未跟踪的 `package/BoardRuntime/NativeLegacyDisplay.cpp.p23-pre-canvas`。此前 iOS 线和生成物未删除。
- 用户授权恢复 iPadOS userdata。设备 `3b25cc7c`，root Recovery，`/dev/block/bootdevice/by-name/userdata` 指向 `/dev/block/sda14`，容量 47938797568 bytes。将此前 iPadOS 基线 `out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img` 的 9382658048 bytes 写入该分区前缀；镜像写前 SHA-256 与设备回读均为 `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`。
- 写前回收 `out/p21/persistent-logs/20261029T150245Z`，boot ID `0x3e1b3491`；userdata 10 GiB 日志窗口写前写后 SHA-256 均为 `76445f9fa52bd266f442b1e0070bfb94e14d50002beac8eead2b60f4681cc98a`。未写其他分区，未临时启动；本次恢复尚无 iPadOS 运行验证。

## 2026-10-29：主线切回 ipados-17.7.11，日志系统已移植

- 分支：当前 `ipados-17.7.11` @ `2508a59`；iOS 16.7.16 线保存为分支 `ios-16.7.16` @ `c1508e6`，已推送到 origin（49 个提交）。`main` 仍指向同一 iOS 提交，两个分支都在。
- 已移植：`scripts/unattended/monitor.py`
  - 2 秒轮询，只使用已经监听的 ADB 端点（默认 5037/5038），绝不启动第二个 server；
  - 判定“root Recovery + ro.bootmode=recovery + cheeseburger”后立即调用 `scripts/logging/read-cheeseburger-log.py`；
  - 以窗口 SHA-256 对比 `out/unattended/state.json`，输出 `NEW_LOG` / `NO_NEW_LOG`。
- 已移植门禁：`scripts/unattended/preboot-gate-p21.py`
  - 校验 packaging → 镜像哈希 → P13LZ001 payload → 展开哈希/长度 → payload 内 kernel 与 `kernel/validation.json` 一致 → 固件哈希与打包一致 → 镜像内 gzip(FD)+DTB 与 uefi 文件一致 → 参数含 `sheng-log=1`、`keepsyms=1`；
  - 已验证：R17 D2、R14 通过；R4 因参数没有 `sheng-log=1` 被拒（R4 是唯一出过 UI 的历史镜像，但要诊断仍需带日志的候选）。
- iOS 线留档要点（详见 `ios-16.7.16` 分支的 HANDOFF）：
  - sandbox kext 移除成功，根因是宽松 MAC policy 必须在 `mac_policy_init` 早期注册（晚注册会写只读静态表 panic）；
  - `lsd` 的 `mobilegestalt` 拒绝从 11 次降到 0；
  - 显示仍 `frames=0`；swap 入口是 selector 5 → Legacy vtable 槽 178 `swap_submit`，selector 72（vsync 注册）从未出现。

## 设备与约束

- 目标设备 `3b25cc7c`，只做临时 `fastboot boot`，不写 boot/recovery/固件/GPT。
- userdata 唯一授权写入：相对 10 GiB、32 MiB 的持久日志窗口（`out/p21/r14/log-window.json`）。
- 每次启动前要有离线门禁；启动后必须能从 Recovery 回收日志（或证明窗口未变）。
