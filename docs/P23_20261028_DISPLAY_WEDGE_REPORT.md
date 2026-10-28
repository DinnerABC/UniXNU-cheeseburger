# P23 Stage C · 2026-10-28 问题报告（iOS 16.7.16 / 20H392 on OnePlus 5）

## 1. 目标与当前结论

- 目标：在 cheeseburger（MSM8998）上启动 iOS 16.7.16 用户态，并推进到系统界面；本阶段定调**只用 CPU 渲染，不适配 GPU**。
- 已达到：内核启动、根卷 + SystemOS owner union 挂载、launchd 与大量系统服务运行（首次进入用户态）。
- 未达到：界面。显示栈能启动，但一旦允许 backboardd 创建 framebuffer 用户客户端，系统在约 120–148 s 出现**整机卡死**；无软件 panic 文本，随后由 PMIC/硬件看门狗以 `pon1`（上电级）复位回 Recovery。
- 稳定基线：显示服务注册、但拒绝 framebuffer 用户客户端时，系统可稳定运行至我们自己的 300 s 复位倒计时（实测到 340 s）。

## 2. 本轮之前的历史问题与已确认修复

### 2.1 固件与 payload 绑定（最强根因，曾导致"进 shell、无内核日志"）

- UEFI 固件在编译期固化：`CompressedPayload.h`（展开 payload 的 SHA-256 与长度）和 `P9Expected.h`（内核/ADT 哈希）。
- 只要 payload/kernel/ADT 改变，**必须重建固件**。此前 r15–r22 与 logfix3 复用旧固件，导致 UEFI 未发布 payload（进 UEFI shell），窗口 boot ID 不变，被误读成"内核没写日志"。
- 修复：`build-payload.py` → `prepare-uefi.py`（复制头文件到 VM 树）→ `p2-linux-build.sh` 重建 → 模块扫描确认三个 pin 编入固件。logfix3b 起每次 payload 变更都重建，日志恢复。

### 2.2 Owner 卷 UUID（EAUTH 80 / fail=104）

- 官方 `build-xnu-4k-baseline.sh` 生成 `build/xnu/owner-data-volume.h`，默认把 `XNU_OWNER_CACHE_VOLUME_UUID` 写成 sheng 的 `8846FE78-…`；20H392 的 SystemOS 是 `96096B28-…`。
- 陈旧 `kern_exec.o` 保留 P21 UUID，导致每轮 `CB owner fail=104 … vm=0`。
- 修复：构建前 `export XNU_OWNER_CACHE_VOLUME_UUID=96096B28-B6DC-4BC7-9D3A-2632A0FE53F1`，并在 gate 中断言 `strings kern_exec.o` 不含 `8846FE78`。

### 2.3 Owner boot manifest（fail=107）

- ADT `/chosen/boot-manifest-hash` 是给 AppleImage4 的全零占位；上游 `offline-owner-boot-manifest` 的"绝不覆盖既有身份"保护拒绝发布。
- 修复：仅允许替换**全零占位**，真实 Apple 身份仍拒绝覆盖（`iokit/bsddev/IOKitBSDInit.cpp`）。

### 2.4 SystemOS 媒体注册竞态（fail=104 的另一半）

- 日志证据：60.204 s `NativeUfs: SystemOS volume … ready`，60.290 s 同 UUID 匹配为空。
- 修复：`CacheAuth.cpp` 对 UUID 匹配做 3 s（60×50 ms）有界重试。

### 2.5 Boot-args 被设备截断（~600–640 字节）

- `CB args:` 显示 `fajita-fb-wc=` 之后变成乱码，后续参数丢失。
- 影响：显示参数原本在 600+ 字节处，`native_mobile_graphics_load_probe` 从未执行（`mgf=-1 stage=0`）。
- 修复：重排参数，显示/PM/transfer/vsync 等关键项全部落在前 ~500 字节。

### 2.6 其它已确认项

- `misc` BCB：日志线程首轮提交后即写入 `boot-recovery`，因此**任何**后续复位（含看门狗）都会回 Recovery，而不是系统。
- `adb get-state` 在 Recovery 返回 `recovery` 而非 `device`，监视器已同时接受两者；ADB 已重置为系统默认单服务 5037。
- 复位取证：心跳 `P23 ALIVE left=N` 证明多轮 ~146 s 停摆**不是**无人值守倒计时；Recovery 侧 `ro.boot.startupmode=pon1` 表明是上电级复位（看门狗/PSHOLD）。

## 3. 显示栈与当前阻塞

### 3.1 已实现（可确认）

- legacy framebuffer 路径可启动：`IOMobileGraphicsFamily` 类注册、`IOMobileFramebufferLegacy::start`、`P23 DISP2 mgf=0 stage=10 meta=0 init=1 start=1 match=1 surf=0`。
- 显示启动序列（msgbuf 实证）：`startHardware enter/ok → registered hz=60 → announcePowered → start ok → setPower ordinal=1`。
- `offline-legacy-pm=0` 解除了**首次客户端创建阻塞**：logfix18 中 `newUserClient type=0 #1/#2` 均成功，随后进入 `getDotPitch #1/#2/#3`。

### 3.2 当前阻塞（未解决）

- 允许客户端后，系统在 120–148 s 整机卡死：心跳与日志同时停止，无 panic/guard 输出，随后 pon1。
- logfix18：`newUserClient ×2 → getDotPitch ×3` 后停止（147.86 s）。
- logfix25（transfer=0，无逐帧拷贝）仍在 ~141 s 停止。
- 因果对照：完全关闭 legacy 显示（logfix26）可活到 ~252 s；拒绝客户端（logfix27）稳定到 340 s（我们自己的倒计时复位）。→ **卡死与"客户端被原始实现使用"绑定，而非拷贝本身**。

### 3.3 已排除的假设（均做过单变量实验）

| 假设 | 实验 | 结果 |
| --- | --- | --- |
| vsync 定时器 | logfix10/29 方向（vsync=0） | 仍卡死 |
| 多核调度 | logfix13 `cpus=1` | 仍卡死 |
| 日志 I/O 周期 | logfix11（去 GPT 读）、logfix22（10 s） | 仍卡死 |
| swap / Data 只读 | logfix20 `sheng-data-persist=2`；对照 P21 基线同样无 swap 却能出界面 | 与卡死无因果 |
| panic 路径 UFS 写阻塞输出 | logfix23 移除 | 仍无 panic 文本 |
| guard 槽位（211/245/246/306/307/338/345） | logfix21 guard 打印 | 无 `P23 DISP GUARD` |
| scanout 映射缓存属性 | logfix24 改回 `kIOMapInhibitCache`（上游默认） | 仍卡死 |
| 逐帧 CPU 拷贝 | logfix25 `transfer=0` | 仍卡死 |
| `announcePowered` | logfix28 跳过 | 仍卡死（上游也有该调用，已恢复） |

## 4. 上游与参考仓库对比

- 官方 `drivers/NativeMobileDisplay`（已通过代理拉取，commit `eb9c949`）：
  - `newUserClient`、guard 列表、`announcePoweredByFirmware` 与本机实现一致；差异主要是 **20H392 专用 ABI 偏移**（request surfaces `+0x888`、rects `+0x11/+0x51`、state setter slot 336 等）与我们附加的 vsync/缩放/日志。
  - 上游能出界面的 Sheng 基线使用 **Adreno/IOGPU + Metal 插件合成**；未找到"CPU-only 渲染在实机出界面"的案例。
  - 上游 CPU-render 相关脚本为模拟器/gdb 分析，显式标注 `original_software_renderer_execution_observed=False`、`desktop_observed=False`。
- 同设备 P21（iPadOS 17.7.11）基线：同样 `Failed to open swap file 30`、空闲页 ~0.9 GiB，仍能显示界面（其显示/GPU 栈与本 Stage C 不同）。
- 结论：CPU-only + legacy framebuffer 在当前实现下无法越过"renderer 接管"这一步。

## 5. 用户态 SIGTRAP 潮（独立问题）

- 无显示长跑与拒绝客户端稳定跑中均出现：`SpringBoard / donotdisturbd / triald / contextstored / liveactivitiesd sig=5`，corpse 满后进程反复重启。
- 与 FBSDisplayMonitor / 渲染依赖相关；当前不具备屏幕证据，崩溃报告写入 Data 卷，Recovery 侧无法读取。
- 不作为整机复位根因（拒绝客户端时可稳定 340 s），但会阻止界面。

## 6. 候选与证据索引（本轮关键）

| 候选 | 镜像 SHA-256 | 日志 |
| --- | --- | --- |
| logfix5（首次用户态） | `aca1dcf5…` | `out/p21/persistent-logs/20261028T092213Z` |
| logfix5u（复现） | 同 logfix5 | `…/20261028T094509Z` |
| logfix8（固件重建 + 显示启动） | `47ce7582…` | `…/20261028T102641Z` |
| logfix18（pm=0，客户端成功） | `c77dfa25…` | `…/20261028T115102Z` |
| logfix25（transfer=0） | `5dad7713…` | `…/20261028T123830Z` |
| logfix26（无显示对照） | `908c8570…` | `…/20261028T124613Z` |
| logfix27（拒绝客户端，稳定 340 s） | `824b8577…` | `…/20261028T134619Z` |
| logfix28（跳过 powered，仍卡死） | `1b1fdc21…` | `…/20261028T135615Z` |

- 固件随每个 payload 重建并用 VM 模块扫描校验 payload/kernel/ADT 三个 pin。
- 设备当前：root Recovery（序列号 `3b25cc7c`，ADB 默认 5037）。
- logfix29 仅完成内核预链接（`prelink-final-logfix29`），**未安装、未打包、未启动**。

## 7. 建议的下一步（待决策）

1. 先保留 logfix27 的稳定基线（显示注册、拒绝客户端），用于不依赖渲染的系统服务验证。
2. 若继续 CPU-only 出界面：需要自建最小 framebuffer 用户客户端（提供尺寸/格式/swap-retire，不进入原始客户端启动路径），或按 20H392 ABI 偏移逐项核对原始客户端请求结构（`+0x888` surfaces、`+0x11/+0x51` rects、`+0x318..+0x32c` masks/背景/模式）。
3. 若改为 GPU 路线：移植上游 NativeAdreno/NativeMetal + IOGPU 预链接（上游有成熟实机案例，成功率高但工作量大）。
4. 取证改进：在保留块中周期写入 alive 时间戳（正常线程上下文），区分"内核全停"与"仅日志线程停"；崩溃原因需从 Data 卷的 crash report 读取（当前不可达）。

## 8. 风险与维护注意

- 多处修复是直接编辑 `tree/.cache/xnu-work`（kern_exec.c、bsd_init.c、debug.c、kern_exit.c、iokit/bsddev/IOKitBSDInit.cpp），**未回写 patch series**，存在可复现性缺口；下一次全量重建可能丢失。
- 固件必须随 payload 同步重建；仅 `OFFLINE_PACKAGING_PASS`/`exit_code=0` 不能证明绑定一致。
- 启动期间禁止 fastboot 轮询/多监视器；镜像必须自带自动回 Recovery 路径。
