# P21 首帧前黑屏：下一步调查指南

更新时间：2026-10-30。面向 OnePlus 5 / MSM8998、iPadOS 17.7.11 (21H461)，目标是定位同一镜像有时出画面、有时长时间零帧的原因。先阅读仓库根目录 `AGENTS.md`、`HANDOFF.md`、`docs/P21_BUILD_GUIDE.md`；本文件的勘误优先于 `docs/P21_BLACK_SCREEN_ANALYSIS_20261030.md` 中对应的旧判断。

## 已知事实与勘误

- d2 镜像 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img`（记录 SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`）曾有 14 帧；另一次同镜像记录为 0 帧。已记录的镜像身份与 Fastboot 接受不等于日志自行证明镜像身份，使用前重新哈希并关联启动记录。
- 旧镜像最新黑屏日志 `out/p21/persistent-logs/20261030T052133Z/`，boot ID `0x4bd8fea6`，962 秒、192 轮、零写错误。`stage=10 clients=5 frames=0`，SWAP/prepare 为 0。最近记录的启动是 touch-window2g，但日志本身不包含可独立核验的镜像哈希。
- 对照成功 `out/p21/persistent-logs/20261029T172431Z/`（`0x5a0ea281`），失败 d2 `out/p21/persistent-logs/20261030T050326Z/`（`0x50e2b156`），以及最新旧镜像失败日志。不要用不同镜像比较证明唯一因果。
- **返回码勘误：`0xe00002c7` 是 `kIOReturnUnsupported`；`kIOReturnNotOpen` 是 `0xe00002cd`**，以实际版本 XNU `iokit/IOKit/IOReturn.h` 为准。成功 d2 的 backboardd selector 50 同样两次返回 `0xe00002c7`，但 selector 4/5 各 14 次成功并出帧，因此 selector 50 的返回本身不是黑屏特有根因。上游 `scripts/check-system-framebuffer-client.py` 将一处 frame-info 查询的该返回解释为 unsupported，尚未证明本机 selector 50 的具体语义。
- 最新旧镜像黑屏中 backboardd 15 次调用里 selector 6/8/88 成功，多个 selector 返回 unsupported，selector 15 返回 `0xe00002bc`。`newUserClient` 成功与 `stage=10` 不证明所有用户态依赖和驱动状态完整。`[display-clients-live]` 行是有上限的快照：最多跟踪 8 个对象、显示其中 3 个；`0/0` 不是对该进程所有对象及历史操作的证明。
- 已知用户观察是系统画面曾出现；触摸仍未证实可用。帧提交缺失优先于讨论缩放与像素复制；但尚不能完全排除显示代码、调度、触摸、设备身份、数据状态的交互。

## 第一步：不重启，先从已有证据定位分叉

1. 分别保存各轮 `retrieval.json`、`decoded/.../latest.txt`、`slots.txt`、`msgbuf.txt`、`diagnostic-analysis.json`、`ui-thread-stacks.json` 和 `iokit-matching.txt`；文件是否存在以实际目录为准。记录 boot ID、时间、镜像哈希与关联证据。读取 `latest.txt` 的 `[mfb clients]` 全量 selector 行、`[display-clients-live]`、`[swap timeline]`、`[processes]` 和线程栈，不只看最后一个 selector。
2. 在成功与失败的同一 d2 镜像中，对 backboardd 的 selector 6 初始成功、4/5 首次调用、进程 PID/线程、服务匹配、退出与重启建立时间线；核对 `slots.txt` 的采样间隔、时钟单位和丢失区间。`in/out` 是调用计数，`enter/leave` 是最后 selector，不是调用次数。确认哪些事件有时间、哪些仅是累计值，不能据此虚构先后关系。
3. 检查 `LegacyDisplayProbe.cpp` 中 `interpose`、`wrappedExternal`、`native_legacy_client_observed` 和 `p21_client_rows` 的对象生命周期、8 对象上限、PID 关联、并发和复用限制；区分 newUserClient、外部方法和 provider open/close，它们并非同一事件。对照 `NativeLegacyDisplay.cpp` 的实际转发路径与固定版本上游对应实现，不把 `NativeGOPClient` 的返回语义套到 IOMobileFramebuffer 客户端。
4. 对比两轮 IOService 匹配：包括 `NativeLegacyDisplay`、HID、IOSurface、IOAcceleratorES、KeyStore 和首次 frame 相关服务；匹配到服务不代表依赖已经可用，未命中也不一定是错误。比对启动日志中最早差异，特别注意环形 msgbuf 可能覆盖早期信息。记录每项是观察、推断还是缺失证据。
5. 检查 selector 4/5/6 的真实 ABI 和参数、用户态发起者及其前置条件；先查固定上游 `.work/p21-official-framework` 和 sheng，再查本机实际 21H461 对象/源码证据。不能只凭编号推测方法含义。

## 第二步：优先符号化已有用户态栈

- 寻找与 **21H461、arm64** 系统根匹配的 dyld shared cache 和组件符号文件；先在已有合法本地输入、挂载点和构建记录中查找，记录路径、版本与 SHA-256。报告曾指出 `/Volumes/DawnUpdate21H461...` 未挂载；现在需重新确认，不能拿 20H392 或其他版本的 cache 代替。
- `out/p21/persistent-logs/<时间>/ui-thread-stacks.json` 由 `scripts/logging/analyze-cheeseburger-run.py` 从日志生成，原始采样也在 `decoded/.../latest.txt`。搜索项目已有 `symbolicate-threads.py` 后阅读它实际接受的参数，不猜命令行。核对地址空间、cache UUID、架构与 slide 的适用范围；PC/LR 不一定属于 shared cache，无法归属时明确标注未知，不对零值或不完整帧编造符号。
- 比较同进程/同功能线程的成功与失败栈：用户态函数、系统调用、Mach 等待对象、重复采样变化和线程寿命；区分单次快照与持续忙等。优先定位 backboardd 未发出首个 selector 4/5 前在等待什么、由哪个进程或内核对象满足。缺失符号时保留原始地址、映像范围和偏移，说明限制。

## 第三步：仅在现有证据不足时增加一次有界诊断

在不改变成功/失败语义的前提下，设计每次启动的紧凑事件环：时间（明确时钟与换算）、boot ID、PID/TID、客户端对象身份与代际、provider、newUserClient 的进入/返回及 type/rc、关键 selector 4/5/6/15/50 的进入/返回和可安全读取的必要参数、匹配到的服务、进程退出或连接关闭。只读采集，记录丢失计数及上限；客户端销毁或地址复用必须处理，避免把两个对象合并；不要在 panic 上下文做会睡眠、分配内存或等待锁的操作。不要以 selector 50 unsupported 直接触发修复。

尽可能同时覆盖关键线程等待点和相关服务状态，让一次启动就能区分：

- **等待用户态服务**：线程栈及等待对象指向具体服务，且该服务未完成或未响应；查其首个错误和上游同版本案例。
- **等待显示连接或生命周期**：同一对象的创建、关闭、状态及调用顺序证实握手没有完成；对照原始 IOKit/上游语义，再决定适配。
- **已提交但呈现失败**：selector 4/5 或 swap 到达且返回明确失败，才针对对应的 IOSurface、状态、format、scanout 与内存映射追查。
- **证据不足**：明确缺失事件与可能的采样盲区，不修改返回值来制造成功，不把偶然重启成功当修复。

所有诊断实现前先看固定 UniXNU 与其他设备同版本案例；仅在有直接证据时适配，不能照搬其他设备地址、硬件参数或 ABI。`sheng-disable-jobs`、清 Data、改变 scale/CPU/内存等均会改变变量，不能作为首轮同时加入的“修复”。

## 构建、实机和验收

按 `docs/P21_BUILD_GUIDE.md` 构建独立候选，记录源码提交、编译日志、最终内核/ADT/payload/固件及镜像哈希；确保固件编入的 pin 与实际输入一致。保留 d2、touch-window2g 和全部日志。设备处于 root Recovery 时先回收上一轮日志，并确认只有系统使用的一个 ADB 服务；核对设备 `3b25cc7c` 和旧监视器。按用户当前授权只做一次 `fastboot boot`，记录输入 SHA 和传输结果。Fastboot OKAY 不证明系统启动；不自动循环重启，也不写 boot/recovery/GPT/其他分区。

候选达到预先定义的观察窗口后，记录画面、帧与关键事件，回 Recovery 取回新 boot ID 日志；比较窗口哈希与启动记录。若再次黑屏，依据上述分支确定首个可确认的阻塞，再做针对性修复。验收至少包含：启动与镜像对应、相关进程正常、首帧和后续帧计数增长、屏幕实际显示、失败路径及新增诊断不破坏原逻辑；重复性必须经设备实际测试，不能由一次成功推断。触摸、按键和手机布局分别验收。

完成后更新 `HANDOFF.md`，记录已确认事实、上游对照、剩余不确定项、构建与设备证据；代码或文档修改按项目要求提交。不要提交 IPSW、系统镜像、签名材料或设备私有日志。
