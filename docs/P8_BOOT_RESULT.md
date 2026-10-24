# P8 实机结果

## 首轮集成测试（修复前）

- run_id：`20261022T104601Z-87a9ed62`
- 实测镜像 SHA256：`42ea23280d01cd87e42ffe2b699f8a3c61b3e32accd95d2d9ae08bd4eba602c8`
- 实测 kernel SHA256：`ec650199d86212bb5fdeb26474472c6655b686863247bf9c7b7502866a489045`
- 实测 ADT SHA256：`228938c6686e4961acf90d649ba6b188547279f5e7bfbaf8dd4e2b82c261a6c6`
- 对应不可混淆的二进制/配置：`out/p8/tested/20261022T104601Z-87a9ed62/`
- 主机完整记录：`out/p8/device-loop/20261022T104601Z-87a9ed62/`；含身份的原始命令输出在该 run 的 private 子目录。

手机身份及解锁状态核对通过；关键恢复分区备份重新计算 SHA 后通过。只执行了 `fastboot boot`，命令退出 0；没有 flash、erase、解锁、GPT 或校准区写入。

手机由 ANDROID_ADB → FASTBOOT → 临时启动后 USB 离线。240 秒观察结束，控制器分类为 `NO_TRANSPORT_MANUAL_RECOVERY`，主命令退出 2。该结果表示主机传输无法恢复，**不等于内核没有执行**。没有自动 EDL、watchdog 修改或重复启动。

用户提供的屏幕 OCR 明确显示持续输出：

```text
unexpected .../.work/p4-unixnu/.cache/xnu-work/iokit/Kernel/IODeviceTreeSupport.cpp:6...
```

这提供了真实 XNU/IOKit 设备树处理的执行证据，也验证了新普通文字输出通道可用。完整行号被 OCR 截断，没有真实 PC trace 或完整 panic 文本。固定源码和实际旧 ADT 能有界复现第 610 行重复警告，因此将首个具体软件故障定位到通用中断描述契约；这项精确行号关联属于源码推断，不冒称手机寄存器证据。

## 已确认与未确认

| 阶段 | 证据与结论 |
|---|---|
| 镜像编译/回读 | 实测版本真实通过；不是运行成功的替代证据 |
| 临时传输 | Fastboot boot 退出 0；只证明被接受 |
| XNU 与 IOKit DT 处理 | 用户屏幕重复源码信息，已确认执行到该路径；沿当前源码调用链来自 kernel bootstrap thread / PE_init_iokit |
| IOKit 完整初始化返回 | 未确认，不能把 DT 处理入口当作完成 |
| ArmDTPlatform / ArmCpu / 定时器中断 | 未获得成功注册或真实 IRQ 证据 |
| UFS / LUN / READ / IOMedia | 模块已编译链接，无实机成功证据 |
| Crypto POST | 实际实现及 seal 保留，无本轮执行成功证据 |
| APFS / 根卷 / BSD / launchd / SpringBoard | 均未确认；当前没有兼容的完整 root 输入 |
| 自动日志回收 | 已保存主机传输记录；XNU 电子日志跨重启回收尚未实现 |

## 修复及下一次验证边界

修正后的 ADT 不再把原始 CPU PPI19 和轮询 UFS INTID297 表示为缺失 controller 图的通用 `interrupts`。ArmCpu 配对读取私有属性，真实 IOCPUInterruptController、GIC 和 rtclock 后端保留。固定 IOKit 映射器没有被修改，断言/检查没有被关闭。实际函数的宿主回归和修正版内核重链接通过；最终修正版镜像哈希见 `P8_HANDOFF.md` 及 `out/p8/uefi/packaging.json`。

**首轮交付时，修正版尚未实机测试；后续已获用户授权测试，结果见下节。** 手机失联后的实机路径已停止；没有假称修复已在设备上生效。下一次必须先人工恢复 Android，并只读确认 ADB 或 Fastboot 身份在线，才能执行针对这项具体故障的临时启动。不得重试旧镜像。

不具备 userdata 持久化安装条件：真实 UFS 读取、SHNGROOT 子窗口、匹配 APFS/可信缓存、根卷及恢复演练均未形成闭环。

## 修复后的针对性测试（已停止）

用户明确确认已恢复 Android 并要求测试。`repaired-detect-01` 已只读确认 `ANDROID_ADB`，修正版镜像校验通过。新的 run 为 `20261022T111743Z-a9a520d5`，镜像 SHA256 `a4458921798976dba7e57f220f78163b488a18cb9d5da5e615c36770986629d2`。仅进行一次新的临时启动；此条记录不代表修复已在实机通过。完整输入冻结在对应 `out/p8/tested/` run 目录。


本次结果：Bootloader 接受修正版临时启动，用户观察文字刷屏一段时间，随后只剩 `MACH Reboot`。未提供此前 panic/驱动文字，未报告原先的重复 `unexpected`，但这不足以证明 IOKit 初始化完整返回。

当前固定源码 `osfmk/arm64/machine_routines.c:804` 在 `halt_all_cpus(TRUE)` 打印这行，再调用 `PEHaltRestart(kPERestartCPU)`；panic debugger 等多个调用路径均可能到达这里。故可确认进入内核重启/停机处理路径，不能由此反推特定 panic 原因、UFS、根卷、BSD 或 launchd 成功。

240 秒观察期后 ADB/Fastboot 仍未恢复，控制器退出 2，分类 `NO_TRANSPORT_MANUAL_RECOVERY`，已停止，无自动复试。观察转录在 `evidence/p8/repaired-device-feedback.txt`，结构化结果在 `out/p8/latest-boot-result.json`。没有执行任何刷写、擦除或解锁。

下一处具体工作是保留重启前的 panic/错误正文。当前控制台的整屏回绕清理可能丢失此前可见文字，此为需检查的日志保留问题，不能冒称已证明是根因。先修复日志保留或停留行为，再对已确认错误作针对性修改；不盲目改 UFS 或禁用检查。手机需要人工恢复并确认在线后，才能进行下一次临时测试。
