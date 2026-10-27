# P23 2 GiB 启动后无桌面：日志、内存与显示 ABI 分析

日期：2026-10-27。只读分析，未修改代码、未发送镜像。

## 1. 本轮证据

日志：`out/p21/persistent-logs/20261027T044622Z`

- boot_id `0x3ab2d3de`
- 65 次提交，uptime 326 秒，write_errors=0
- 窗口未覆盖（ring_overwritten=0）

关键状态：

```text
MEM managed=2048MiB free=1365 wire=198524 active=106029 inactive=105418
DISPLAY stage=10 meta=0 reg=1 clients=12 frames=0
SWAP check=0 reject=0 why=0 transfer=0 rc=ffffffffffffffff
FB2 backboardd p55 in/out=7142/7142 enter/leave=5/5 rc=e00002c2
  sel4/5/6=3560/3560/3 notify72=0 err72=0
FB5 SpringBoard p26 in/out=2/2 enter/leave=8/8 rc=0
FB6 SpringBoard p26 in/out=1/1 enter/leave=8/8 rc=0
```

进程：

```text
launchd p1, SpringBoard p26, backboardd p55,
chronod p95, liveactivitiesd p131, lsd p96, ...
```

## 2. 2 GiB 内存结论

- `managed=2048MiB`，说明 2 GiB 高 bank 窗口实际生效。
- `free=1365` 是 4 KiB 页数（约 5.3 MiB）；`inactive=105418` 页（约 411 MiB），可回收余量比 1 GiB 时明显好。
- 本窗口未见新的 `killing_idle_process` / `killing_top_process` / `system is unhealthy` 记录；服务不再被持续 jetsam。
- 因此**当前问题不是内存不足**。暂不要再升 2.5/2.75 GiB，避免把显示问题与内存变量混在一起。

## 3. 当前不是“没启动”，而是显示首帧没有提交

- SpringBoard、backboardd、chronod、liveactivitiesd 均已存在；
- 无 fatal/panic，系统服务持续运行；
- `clients=12`，说明显示用户客户端已建立；
- 但 `frames=0`、`SWAP check=0`、`transfer=0`，说明**没有一帧通过 swap 验证/提交**；
- `[display]` 的 `Current Display` 字典为空，说明显示模式/当前 display 尚未成功建立。

`e00002c2` = `kIOReturnBadArgument`（`IOReturn.h` 的 `iokit_common_err(0x2c2)`）。

backboardd 的 `FB2` 在 7142 次进出中，selector 4/5 各被调用 3560 次，最后返回 bad argument；SpringBoard 的 FB5/FB6 只做了 1–2 次调用。这是**显示用户客户端 ABI/模式不匹配**的表现，不是 SpringBoard 没启动。

## 4. 与 R7 成功基线对比

R7（iPadOS 17.7.11 用户态 + 同代 17.7.11 kext）：

```text
clients=8 frames=5
SWAP check=5 transfer=5 rc=0
FB2 backboardd rc=0 sel4/5/6=5/5/13
```

当前（iOS 16.7.11 用户态 + 17.7.11 kext/后端）：

```text
clients=12 frames=0
SWAP check=0 transfer=0
FB2 backboardd rc=e00002c2 sel4/5/6=3560/3560/3
```

同一后端在 R7 能接受 5 个 swap；换成 iOS16 用户态后 selector 4/5 持续 bad argument。最有力的解释是：

- iOS16 `backboardd`/`IOMobileFramebuffer.framework` 与 17.7.11 的 `IOMobileGraphicsFamily/IOMobileFramebufferLegacy` 用户客户端 ABI 不同；或
- iOS16 期望的 display mode/尺寸/格式与当前后端发布的 `1080×1920` 不一致。

目前日志只能确定“backboardd selector 4/5 bad argument + 无 swap”，还不能区分是参数布局还是 mode/format。

## 5. 上游与其他仓库对照

### 5.1 同代配对原则

- ios-topaz：XNU 8796 + iOS16 用户态 + 自己的 AppleCLCD/显示后端，同代配对；另有 iPhone X 显示尺寸补丁。
- ios-sailfish：XNU 8796 + iOS16 用户态 + iPhone 8 显示后端，同代配对。
- 本机是 **XNU10063.141.1 + 17.7.11 kernelcache kext + iOS16.7.11 用户态**；显示 kext 与用户态不同代，这是当前最可疑的结构性差异。

### 5.2 iPhone X 显示尺寸案例

- ios-topaz 的 `0061-sweet-iphone-x-display-size.patch`：向用户空间发布 **1125×2436** 逻辑画布，保留真实 panel，并用偏移/裁剪映射。
- `0114-topaz-iphonex-canvas-aligned.patch`：对其 panel 的 4 像素/16 字节对齐；该参数不能直接照搬到 MSM8998。
- 本机 P21 文档已记录：iOS16/iPhone X 的 SRC/DST 可能不是 1080×1920；当前 `NativeLegacyDisplay` 已有有界缩放路径，但**尚未有任何 swap 到达它**。

### 5.3 当前后端

`NativeLegacyDisplay`：

- `startHardware()` 用 GOP 实测 1080×1920 安装 boot dimensions；
- `getDisplayDimensions()` 返回该尺寸；
- `validateSwap()`/`kickOnce()` 支持同尺寸 CPU copy 和有界缩放；
- 但 iOS16 backboardd 在 selector 4/5 已失败，因此根本走不到 `validateSwap`。

## 6. 日志是否正常

正常部分：

- 内核/root/SystemOS/Data/launchd 启动链正常；
- 2 GiB 内存生效，无新的 jetsam；
- SpringBoard/backboardd 存活，无 fatal/panic；
- 大量 Sandbox deny、`TVRemoteConnectionService` 被 profile 杀掉、`ReportCrash/chronod` wake 警告是已知非致命噪音，不是本次无桌面的直接原因。

不正常部分：

- 显示 `Current Display` 空；
- backboardd selector 4/5 持续 `e00002c2`；
- swap 从未开始；
- 因此没有首帧，桌面不会出现。

## 7. 下一步修改计划（先不执行）

### Phase A：只读诊断，确认 ABI 还是 mode

在现有显示探针中增加有界记录：

- selector 4/5/6 的 `scalarInputCount`、`scalarOutputCount`、`structInputSize`、`structOutputSize`；
- 前几个 scalar 值、请求的 display mode/尺寸/format；
- 返回码和调用 PID；
- 只记录首个失败和统计，不刷屏。

同时静态对照：

- iOS16 `IOMobileFramebuffer.framework`/`backboardd` 的 selector 调用；
- 17.7.11 `IOMobileFramebufferLegacy` user client 的 dispatch/参数布局；
- `getDisplayDimensions`/mode 发布路径。

验收：能明确回答“selector 4/5 的参数布局不同”还是“mode/format 不被接受”。

### Phase B：mode/画布兼容

如果确认是 iPhone X 逻辑画布/尺寸：

- 参考 ios-topaz 0061/0114，但只实现一个有界映射：
  - 用户态发布 iPhone X 期望的 1125×2436（或实际请求的 mode）；
  - 物理 scanout 仍为真实 1080×1920；
  - `NativeLegacyDisplay` 验证源/目标矩形、格式、stride、边界；
  - 用已有有界缩放/裁剪路径把源映射到 GOP；
- 不伪造 pixel format，不删除边界检查。

### Phase C：selector ABI 兼容

如果确认 selector 4/5 的参数结构不同：

- 只对明确的 iOS16 旧 selector 参数做翻译；
- 保留 17 路径、参数数量/大小/类型验证和错误返回；
- 不把 `e00002c2` 直接改成 success，不跳过端口/模式安装；
- 与 Phase B 的 mode 修复放同一候选，避免先修 selector 又卡 mode。

### Phase D：首帧验证

- 保持 2 GiB 窗口、8 核、Data/log 不变；
- 一次临时启动，检查：
  - selector 4/5 返回 0；
  - `Current Display` 非空；
  - `SWAP check` 增加；
  - `frames`/`transfer` 增加；
  - 真实像素内容（非黑）和屏幕照片；
- 若仍无帧，再按 `validateSwap` reason/`DISPLAY COPY FAILED` 定位，不先改内存。

### Phase E：Apple 标志

- 不在本阶段追 Apple logo；RAM 启动链没有 iBoot logo 展示服务；
- 以 SpringBoard 首帧/桌面作为目标，logo 不是启动成功的必要条件。

## 8. 结论

- 2 GiB 内存扩展成功，内存压力已明显缓解；当前不是内存问题。
- 系统已进入 SpringBoard/backboardd，但没有显示首帧。
- 首个具体显示错误是 backboardd 的 `kIOReturnBadArgument`（selector 4/5），与 iOS16 用户态/17 kext 的显示 ABI 或 mode/format 不匹配一致。
- 下一步先做 selector 4/5 参数诊断，再决定是 mode 画布修复还是 selector 兼容；不先改内存，也不直接放宽错误。

## 9. 2048×2732 画布验证结果（2026-10-27 追加）

日志：`out/p21/persistent-logs/20261027T051128Z`，boot_id `0x3aa2ceff`，80 次提交。

画布确实已经发布到用户态：

```text
[display]
D001 width=0x800 (2048) height=0xaac (2732)
```

说明 `fajita-display-logical=2048x2732` 已生效，`NativeLegacyDisplay` 的
`installBootDimensions/planScale/publishLogical` 路径工作。

但显示仍停在：

```text
DISPLAY clients=11 frames=0
SWAP check=0 reject=0 transfer=0
FB2 backboardd p55 in/out=26140/26140 rc=e00002c2
  sel4/5/6=13059/13059/3
FB6 SpringBoard p26 in/out=2/2 rc=0
  sel4/5/6=0/0/0
```

对比无画布版本：

```text
sel4/5/6=3560/3560/3
```

画布改动后 selector 4/5 调用次数随运行时间增加，但返回码仍是
`kIOReturnBadArgument (e00002c2)`。因此：

**画布尺寸不是当前根因；backboardd 在 selector 4/5 的请求/ABI 层已经失败。**

selector 4/5 很可能是 legacy swap begin/end；selector 6 是 swap wait（见
`NativeLegacyDisplay` 中“wait (selector 6)”的注释和 negroni 自定义 framebuffer
的 selector 列表：5 SwapEnd、6 SwapWait）。如果 4/5 失败，原始 family 不会进入
`validateSwap`，所以 `SWAP check=0`、`frames=0`。

## 10. 上游跨版本证据

- `out/p20/upstream/fajita/NOTES-ios16.md` 明确记录：iOS16.7 用户态需要匹配的
  `xnu-8796` 系列和按 20H392 kernelcache 编译的 prelinked kexts；当前 10063 的
  iOS17 kext 与 iOS16 用户态不是同代配对。
- ios-topaz / ios-sailfish 都是 XNU8796 + iOS16 用户态 + 同代显示后端。
- negroni 的 `NegroniFramebuffer` 是自定义 IOMFB，不是 17.7.11 原始
  `IOMobileFramebufferLegacy` 的 selector ABI；其 selector 注释（5 SwapEnd、
  6 SwapWait）只能证明 selector 语义，不能直接拿来当 17 kext 的兼容实现。
- 因此 2048×2732 画布只解决“用户态看到的尺寸”，不能解决“iOS16 用户态发送的
  selector 4/5 请求结构/字段与 17.7.11 kext 不一致”。

## 11. 后续方案（先不执行）

1. **诊断 selector 4/5 参数**：记录 scalarInputCount、structInputSize、前几个字段、
   return、PID；比较 iOS16 和 17.7.11 的请求结构。没有这一步，不能安全写 shim。
2. **优先同代方案**：按 fajita NOTES-ios16 的方向准备 xnu8796 + 20H392 kernelcache /
   kexts；这是结构性正确解，但工作量大。
3. **局部 selector shim**：只当能明确拿到 iOS16/17 的 4/5 结构差异时，在
   `NativeLegacyDisplay`/user client 层做有界翻译；保留原错误，不伪造成功。
4. **iPhone X identity/1125×2436**：属于下一个变量；当前 2048×2732 已发布但 selector
   仍失败，先不要继续换画布。
5. 内存保持 2 GiB；当前不是内存问题。
