# UniXNU-cheeseburger P21：Display Swap 源码级 Review 与上游实现对照

审查日期：2026-10-25（Asia/Shanghai）。范围：P21-R2 实际产物、同版 21H461 原始 IOMobileGraphicsFamily、NativeMobileDisplay、显示探针及指定其他设备的公开实现。

**本轮只新增本报告。没有修改源文件、既有证据、Data seed 或镜像；没有编译、生成候选、启动 QEMU、运行 fastboot、重启设备、更新或重置工作树。** 网络读取限于提交信息和源码原文。二进制分析直接在内存中读取现有 Mach-O；不生成新的反汇编文件。

## 审查结论

1. **尚无证据把 1,587 次拒绝直接归因于一个已确认的 offset bug。** 实际最终内核确实读取偏移 17、81 的两组四个 32 位字段。本轮在同版原始 Apple 代码中独立找到了这两组字段的生产与消费；偏移 17 对应源矩形，原始代码明确计算 `x + width`，不是把右下角坐标当宽度。当前 memcpy 对 packed 数据的读取没有发现明显 padding 或 64/32 位错误。
2. **完整画面、零原点、源目标同尺寸，是当前 NativeLegacyDisplay 复制实现的功能限制，不是原始 Swap ABI 的普遍合法性条件。** 原始家族代码有源矩形边界检查，上游还有专门的非等尺寸负向用例。合法的裁剪、偏移或缩放请求可能被当前后端拒绝；不能因此直接删除验证。
3. **“三个 Options 都为零”的既有描述不准确。** 原始 21H461 `swap_submit` 将请求偏移 812 称为 `dummy`，原始完成代码显式区分其 0、15 和其他值；不能将 804/808/812 统称为已解码的 orientation 或同步 flags。要求它们全零是保守的已实现子集，不是“任何非零都违反 Apple ABI”的证据。
4. **单纯的 request width/height 与物理屏幕不一致，优先级应低于矩形和上述三个原始字段。** 原始生产者把同一显示对象 `+5808/+5812` 写入请求 `+816/+820`，当前 getter 又从同一对成员取尺寸。正常、同一对象、尺寸稳定的路径下，两者应一致。逻辑 scale 不能直接当作这里出现半尺寸请求的证明。
5. 本地渲染源码与本轮可读取的官方 main **逐字节一致**，但这不能证明其满足本机原始图形服务所有合法请求。未找到一个公开、可直接合入的 `reason=3` 专项修复。
6. 当前最值得优先核实的三个触发类别为：**非完整画面/源目标映射；非零 dummy 或另外两个尚未解码的字段；mode/方向/scale 或对象实例一致性。** 这是排查优先级，不是三个已经成立的根因。没有被拒绝请求的实值，不能在前两者之间做有证据的概率排序。

## A. 当前状态与证据边界

项目根目录：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`。设备 OnePlus 5 / MSM8998；参考用户空间 iPadOS 17.7.11 / 21H461。

已阅读 [P21_HANDOFF.md](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/docs/P21_HANDOFF.md)、[既有拒绝分析](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/audit/20261025-swap-reject/analysis.md)、运行快照、构建/链接/资格报告、实际探针、ADT、载荷配置及最终内核。本地没有 `docs/P21_BOOT_RESULT.md`；因此没有把不存在的独立报告当成证据。

| 项目 | 已有证据 | 不应推导的结论 |
|---|---|---|
| 验证/拒绝 | 用户转录：check=1589，reject=1587，约 99.87% | 没有逐请求样本，不能说所有历史拒绝都是 reason=3 |
| 最近拒绝 | why=3 | 不能区分矩形、三个原始字段、请求尺寸中的哪项失败 |
| 传输/帧 | transfer=2，最近 rc=0，frames=2 | 可能包含纯黑填充，不能证明有非黑 GUI |
| UI 进程 | SpringBoard/backboardd 采样时存在，匹配致命退出为 0 | 不能证明之后持续健康，也不能证明其已完成全部初始化 |
| 蓝字恢复 | 用户明确报告黑屏后能看到 R2 蓝字 | 支持诊断恢复取得实机效果，不是系统 GUI 成功 |
| 观察器时间 | obs=552s | 从观察器启动计时，不是完整上电耗时 |

P21-R2 镜像 SHA-256：`75528e95881bed61ceecdac908f0def3ccbf68be9774839dab926fa44034b180`，82,460,672 字节。

最终 kernel.macho SHA-256：`d26aa1c25a35c2c83bc6ade105ec63a9cbde8dc0de024d807dae64d575cf0337`。本轮重新读取文件核对了该哈希。

原始 kernelcache SHA-256：`f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`。最终内核所带原始 IOMobileGraphicsFamily 的 `__TEXT_EXEC` 为 225,140 字节，SHA-256：`2e044ee68c1650397e1435234ca6459206262e4c7ed709cb7131c6f664f9bd9b`；本轮按实际 segment/file offset 重新计算，符合原有同版审计。

证据等级在下文统一使用：**已证实**（本轮源码/实际二进制可直接验证）、**高可信兼容性风险**（确定的限制可拒绝一类合法请求，但缺本机实值）、**可能**（路径合理但缺本机证据）、**需动态值**（静态材料不足以确定）。

## B. 来源、实际编入版本与上游对照

### B1. 本地构建来源

| 层级 | 本轮核实结果 |
|---|---|
| 组装框架工作树 | `.work/p20-r11/framework` HEAD `c62bf12595f9a86c1b7282cf12d5af40b441fdac`；存在板级修改/未跟踪集成文件，不能声称它是未修改官方 checkout |
| NativeMobileDisplay 子模块 | HEAD `8666a9950c9699b368931d18cac92c0fdb2ce9c0`，本轮该子模块 `git status --short` 无输出；框架 HEAD 的 gitlink 也指向该 commit |
| 实际渲染源 | `NativeLegacyDisplay.cpp` SHA-256 `09d7811db8fc6b659e12a05690867326e851f0d11bec7ffbb36b72c33ea035ca` |
| 请求/复制辅助源 | `NativeLegacyRequest.h` SHA-256 `81e89e1f40f2f1d0919567f53bc0564c650488843082693d00b9ea256cafae21` |
| 呈现器源 | `NativeGOPPresenter.cpp` SHA-256 `7fcf53e5a971ad07d9259b93cc8eb4ecbce6962e5a1356c9cc7b39b1264becc0` |
| R2 实际渲染对象 | `NativeLegacyDisplay.late.o` SHA-256 `b9b6dd4a79488ddb4e2d0efe063bde74c18f3eab4047c9203f0985478e8b3277`，与 R1/框架已编译对象一致 |
| R2 变化所在 | 单独编译 `out/p21/r2/source/LegacyDisplayProbe.cpp` 及诊断桥；渲染实现未重写 |
| 最终链接 | R10 核心输入基础上，用 R2 的 `libNativeLegacyDisplay.nokeystore.a` 替换旧显示归档，force_load 进入最终内核 |

出处：[build-report.json](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/build/native-legacy-display/build-report.json:2)、[实际对象哈希](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/audit/rendering-objects-preserved.json:3)、[探针编译/归档脚本](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/scripts/build-display-adapter.py:3)、[链接替换](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/scripts/build-r10-overlay.py:26)。这些脚本本轮只阅读，没有执行。

### B2. 官方与其他设备版本

以下“当前”指本轮通过 GitHub 提交接口可读取到的提交；源码按固定提交或已有本地固定 checkout 阅读，没有 fetch 或更新任何工作树。GitHub API 后续出现 403 限流，不影响已经取得的提交号和 raw 固定版本读取。

| 仓库 | Commit | 实际阅读的相关实现 | 与本地差异及适用性 |
|---|---|---|---|
| 官方 NativeMobileDisplay，本地 pin | `8666a9950c9699b368931d18cac92c0fdb2ce9c0` | NativeLegacyDisplay / Request / GOPPresenter | 本机实际渲染对象来源 |
| 官方 NativeMobileDisplay，当前 main | `cf3b42979a9c44219e7bb149543e6fc3388b58ed`；2026-10-24 18:03:53 UTC | 固定版本 raw 三个文件 | C++、Request.h、GOPPresenter.cpp 的 SHA-256 均与本地一致；未发现本函数尚未同步的修复 |
| Pagani，历史 UI 成功版本 | `69952a98c9180da1d8e579c9c9bcb13df5b12a4b` | Stage7Probe.cpp、Stage7 编译脚本、native-mobile-display-stage7.patch、upstream.rev | 复用 Legacy/CPU/GOP；补丁处理软件 vsync/队列，没有修改 validateSwap。成功是整组集成结果，未归因到 reason=3 |
| Pagani，当前 main | `c27ed34c7c4d5192f18d851c1f879c01722a700f`；2026-10-25 08:11:37 UTC | board.toml、porting.toml、README | format-2 迁移后主线仍选 NativeMobileDisplay/Stage7Probe；记录要求对迁移镜像另做实机验证，不能将旧 Build 39 的成功转移给新镜像 |
| UniXNU-qemu | `97b75b1f63c6b7e895cd7646029db741d5fe5488` | package/LegacyDisplayProbe.cpp、board.toml；相关原始协议/几何验证脚本 | 探针同样读 17/81 等字段，仅观察不改 verdict；设备仓库不内置独立 NativeLegacyDisplay fork，不能当成 reason=3 修复证据 |
| UniXNU-sheng，已有可读实现 | `251b291adf941b9953fe3ae66a808b9b626e164d` | package/LegacyDisplayProbe.cpp、FramebufferGeometryDiagnostic.cpp、OfflineLegacySwap.mm、验证脚本 | 提供原始 API 的正/负几何测试及 unit-scale 策略；未见解除全帧验证的板级补丁 |
| UniXNU-sheng，当前 main | `0e1f6e52638137db15905db8d775ad73757f39fa` | 固定版本 board.toml / dts/sheng.dts | 配置仍选原始 Legacy 路径；scale=2 与本机 scale=1 不同，但数值不是可复制的通用修复 |
| ios-topaz | 默认分支可读取 HEAD `e4772911e116ab4ab6c4ac1f38e6106fefe8b483` | README、NOTES、该提交树中的实际 `xnu/patches/0046-sweet-iosurface-iomfb.patch` | iOS 16 SweetSurface/AppleCLCD 自有用户客户端，不是 iPadOS 17 原始 Legacy ABI。其局部坐标复制可作功能参考，字节偏移不能搬用；后续补丁继续演进，0046 不是当前整套补丁应用后的完整驱动 |

官方源码固定链接：[NativeLegacyDisplay](https://github.com/UniXNU/NativeMobileDisplay/blob/cf3b42979a9c44219e7bb149543e6fc3388b58ed/NativeLegacyDisplay.cpp)。对应文件本轮与本地逐字节比较；不是只比较函数名或 README。

Pagani 旧仓库的 `upstream.rev` 是 `d9460ebbbf43dd7f21b0ef3f62e9437c35b2315c`，而非本机框架 HEAD；本地没有该完整上游 commit tree 可用于重新闭合 Build 39 的每个子模块 gitlink。本报告不将“相同名字的 NativeMobileDisplay”当作成功镜像的独立二进制身份验证。已确认的是其实际 Stage7 调用/集成补丁及没有公开的 reason=3 专项处理。

## C. 调用链与同版二进制核对

### C1. 实际路径

`原始 iPadOS 显示服务` → 用户态 IOMobileFramebuffer/IOSurface 调用 → 原始 IOMobileGraphicsFamily 构造内部 request、解析 IOSurface → 原始家族通过虚函数 slot 296 调用 NativeLegacyDisplay::validateSwap → accepted 请求进入已验证队列 → kickSwapQueue/kickOnce → CPU 映射和复制或纯黑填充 → 原始完成/退休路径。

本机 `offline-legacy-phase=4`，没有 `sheng-fb-gpu=1`；因此 **NativeGOPPresenter 不是当前每次显示提交都必须经过的中间节点**。它的类和创建入口被链接/初始化，但 phase 3 的独立服务和 phase 4 的 GPU handoff 均未在当前参数中选中。当前普通帧由 NativeLegacyDisplay 直接复制到保留的 GOP 映射。出处：[实际 phase 分支](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/source/LegacyDisplayProbe.cpp:529)、[CPU/GPU 分流](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:292)。

`reason=3` 路径已经通过 IOSurface 类、单层状态、主显示/PM/GOP 的前置门槛，尚未进入 descriptor、格式和像素内存范围检查。这使 CPU copy 速度、GOP 写入快慢、刷新率本身不是“最近 reason=3”的直接触发条件。

### C2. 最终机器码不是另一份隐藏实现

本轮读取 P21-R2 最终 kernel 的符号及实际指令，NativeLegacyDisplay::validateSwap 符号为 `0xfffffff00bf5c2a4`。以下均为该文件内未附加运行时 slide 的静态 VA，不是从照片猜测的 live PC：

| 最终内核静态 VA | 实际操作 | 对应源码 |
|---|---|---|
| `0xfffffff00bf5c39c` | `ldur w8, [x19,#0x11]` | 矩形 A 第一个 32 位字段，offset 17 |
| `0xfffffff00bf5c3d0` | `ldur w11, [x19,#0x51]` | 矩形 B 第一个字段，offset 81 |
| `0xfffffff00bf5c3f8` / `...c400` | 读取 `#0x324/#0x328` | 804/808 |
| `0xfffffff00bf5c6c8` | 读取 `#0x32c` 并对非零分支 | 812 |
| `0xfffffff00bf5c6d0` / `...c6dc` | 读取 `#0x330/#0x334`，与 getter 的两个输出比较 | 816/820 |
| `0xfffffff00bf5c6f0` | 调用原始 IOSurface descriptor 导入 | 通过 reason=3 后才进入 backing 检查 |

这补足了旧 `audit/display-entry.asm` 仅反汇编探针入口的局限，证明最终产物确实编入本报告所审查的条件。本轮还读取最终 `__ZTV19NativeLegacyDisplay`（静态 VA `0xfffffff0072060a0`）：跳过两个 Itanium vtable 头项后的 slot 296，指针确为 `0xfffffff00bf5c2a4`，闭合了原始虚调用、native vtable 和上述函数的静态关系。它仍不能提供本次 live request 的实值。

### C3. 原始 Apple 生产者/消费者对 ABI 的独立支持

对最终内核中哈希不变的原始 IOMobileGraphicsFamily，按 coalesced segment 的实际文件偏移读取，使用现有 libLLVM 的 AArch64 反汇编接口在内存中解码。普通 objdump 默认只显示 native `__TEXT_EXEC` 时看不到这些原始合并段，不能因无输出就说原始代码不存在。

| 原始家族的最终静态 VA | 本轮看到的操作 | 可得结论 |
|---|---|---|
| `0xfffffff005b62adc`–`...62af4` | 从外部输入结构 `+172 + layer*16` 读 16 字节，写入内部 request `+17 + layer*16` | 偏移 17 是真实的内部 packed 数组，不是 C++ 默认 padding 猜值 |
| `0xfffffff005b62b3c`–`...62b54` | 输入 `+268 + layer*16` → request `+81 + layer*16` | 偏移 81 是另一组真实矩形数组；内外两种布局不能混用 |
| `0xfffffff005b63268`–`...6331c` | 读 `+17` 的 signed x，拒绝小于零；将 x 与 `+8` 字段相加，再与 IOSurface 宽度比较 | A 为源矩形，第三字段是 width；原始错误字符串为 `srcRect.x (%d) less than zero` |
| `0xfffffff005b61adc`–`...61b74` | 对两组矩形的 `+8/+12` 字段作非零检查 | 不是整个 CGRect double 结构直接搬入内部数组；此处按 32 位 extent 消费 |
| `0xfffffff005b625b8` | 从输入结构 `+344` 写 request `+804` | 804 有真实原始生产者，但本轮未给它未经证明的语义名 |
| `0xfffffff005b625c8` | 从输入 `+380` 写 request `+812` | 812 也不是 padding |
| `0xfffffff005b62688`–`...626ac` | 读取 request `+812` 作为日志第三个整数；格式为 `swap_submit enabled: %08x completed: %08x dummy: %d` | 原始实现称该字段为 dummy；不能一律描述成几何变换 flag |
| `0xfffffff005b5de38`–`...5de78` | 读取 current request `+812`，对 0 和 15 执行明确分支，涉及完成代数 `self+5820` | 非零值存在原始语义处理；不能由本地零值门槛推导 Apple ABI 只允许零 |
| `0xfffffff005b625cc`–`...625e0` | `self+5808/+5812` → request `+816/+820` | 这两个请求尺寸由显示对象生成，而非直接来自用户传入的逻辑点宽高 |
| `0xfffffff005b72cbc` / `...72ce4` | getter 读取 `self+5808/+5812`，向两个 uint32 输出写回 | 与当前 installBootDimensions/getDisplayDimensions 使用同一对成员 |
| `0xfffffff005b63ebc`–`...63ed4` | x0=self、x1=request，取 vtable `#2368`（296×8），调用后只消费 w0 bit 0 | 当前 override 的参数/布尔返回约定有同版机器码支持 |
| `0xfffffff005b68bb4`–`...68bcc` | 原始基类 slot 296 返回 false | 通用基类没有规定本地这组 full-frame 条件；具体后端需要定义支持范围 |

**没有将 812=15 直接认定为本机当前值，也没有认定它必然应走像素复制。** dummy、完成等待与 discard/retire 路径需要一并理解。原始代码对非零有分支，只证明它不是无意义垃圾，不证明本机后端已实现其行为。

## D. Rectangle、Options、尺寸及内存边界 Review

### D1. Rectangle：读取位置正确，支持范围严格

位置：[validateSwap 413–434 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:413)。

两组数组是内部 request 的 layer 0：A=17，B=81，各层步长 16 字节。当前只支持 layer 0，因而没有遗漏 layer index 的乘法。原始生产者和消费者提供了比探针注释更强的偏移证据。

当前 `uint32_t rect[4]; memcpy(...)` 避免了源语言对未对齐 `uint32_t*` 的要求；最终机器码确实使用 packed 地址的 LDUR。原始 request 是普通内存，不是把 framebuffer device memory 用未对齐地址读取。没有发现这里由于 offset 非 4 倍数而必然 fault 或取错字段的证据。

原始代码对源 x 使用 signed 检查，当前读取为 uint32；在当前“必须等于零”的约束下，负值转成大正数仍会拒绝，不会因此被错误接受。将来支持负目标坐标裁剪时必须按 signed 语义处理；源坐标不能直接把负值当 unsigned 参与指针计算。

上游自己的 [FramebufferGeometryDiagnostic.cpp 8–35 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/sheng-current/package/FramebufferGeometryDiagnostic.cpp:8) 区分了用户态的四个 double（ARM64 HFA 参数）与内核 request 的四个 32 位字段。它通过原始 `IOMobileFramebufferSwapSetLayer` 提交源 799×600、目标 800×600；[检查脚本 42–63 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/sheng-current/scripts/check-native-legacy-swap-validation.py:42) 明确把它作为 reason=3 的负向控制。该脚本描述依赖的原始 gdb-stop JSON 并未在本机 P21 目录保存，不能把脚本文字冒称本轮重新运行了该实验。

这说明 full-frame equality 是**故意保留的功能门槛**。当前 `kickOnce()` 和 `native_legacy_copy_pixels()` 只会从源左上角开始，按显示宽高复制到目标左上角，根本不接收 source/destination x/y。因此矩形验证与后续复制能力一致；删除它会把不支持的裁剪/缩放请求按错误坐标呈现，甚至破坏边界前提。

可以规划支持的子集：单层、无旋转、无缩放、源目标 extent 相等，且源矩形和目标矩形分别在真实 descriptor/scanout 内。不同 extent 需要重采样；90/270 度需要旋转映射；多层/非黑背景需要组合。局部内容是整层矩形还是 damage 区域，还决定是否保留旧像素或清黑未覆盖区域，不能仅凭两个矩形小于全屏就选一种策略。

### D2. 804/808/812：零值策略不能替代语义解码

位置：[433–434 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:433)，纯黑分支也在 [457 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:457) 使用相同约束。

实际读取类型是三个 32 位 word；没有定义一个对应 Apple 类型的 Options 结构。804 和 812 已找到原始写入；808 在原始家族的其他等待/队列路径也有读取及与常数比较，但本轮没有完成它的全部生产数据流，因此保留原始偏移名。

812 的 dummy 名称与原始非零处理，已推翻“这三个 word 非零就一定非法”的说法；但仍不足以把它们解析成 rotate/scale/sync 三个 flags，更不足以构造允许位掩码。零值检查是后端保守限定，不是独立证明原始请求错误。

如果实值显示只有 dummy 非零，应先查原始 dummy 对等待、代数和 completion 的要求，确定它是否根本不需要复制，还是需要特殊退休路径。不得仅把 812=15 加入条件，然后继续无条件整屏复制；不得将 804/808 的未知位全部忽略。

本轮没找到同版上游已经完整解码这三个字段并提供通用处理的公开实现。ios-topaz 的用户态 wire fields 不是这三个内部字段的替代定义。

### D3. Display size / orientation / scale

| 项目 | 代码与证据 | 本轮判断 |
|---|---|---|
| GOP 宽高/stride | DarwinBoot 读取真实 GOP，通过 boot video 交接；探针用 PE_current_console；显示 mapping 长度须等于 rowBytes×height | 不是从面板规格直接猜 stride 的实现 |
| 本机已知模式 | 既有屏幕输出为 1080×1920、rowBytes=4320、32bpp；诊断绘制亦受 4320/1920 范围检查 | 属已有屏幕/静态配置证据，本轮没有重新采集 live GOP 结构 |
| NativeLegacyDisplay 尺寸 | startHardware 先 installBootDimensions(video width,height)，再用原始 getter 回读相等才接受 | 正常初始化有闭环；没有静态发现固定为 iPad 分辨率的代码 |
| 请求 816/820 | 原始 swap_submit 从同一对象尺寸成员写入 | 不能简单解释成逻辑点坐标；若真的与 getter 不同，要查对象、时序和 ABI，而不是先改 scale |
| IOSurface 尺寸/pitch | getter 来自同版原始 IOSurface；允许 surface width/height 大于显示；stride 独立读取 | padded row 本身不是 reason=3；只有矩形或前面字段不合才会在此阶段失败 |
| ADT | 本轮解码 P21-R2 固定 ADT，未找到静态 display-scale/display-rotation/video 属性；它保留 R10 ADT | 不能仅检查 ADT 文件就声称运行时没有 scale |
| 动态 scale | [探针 417–443 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/source/LegacyDisplayProbe.cpp:417) 在 GOP 映射回调里发布 /chosen/display-scale、display-rotation 并回读 | 本机参数选择 scale=1；值是 UI policy，不会使复制器自动缩放 |
| rotation | startHardware、探针及 Presenter 均拒绝非零 PE_Video rotation；动态属性写该零值 | 没有实现旋转。用户态 layer 的方向/变换仍需查请求，不能由 PE_Video=0 推导所有内容都无旋转 |
| 原始 mode 对象 | 相关上游 [mode 检查脚本](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/UniXNU-current/scripts/check-original-display-mode.py:28) 验证过 800×600、scale 1、60Hz 的原始返回对象 | 是上游实验的证据要求；本机未保存同样的 SpringBoard 返回对象，不能宣布完全等价 |

源位置：[startHardware 489–508 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:489)、[生成 ABI 的 installBootDimensions](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/build/native-legacy-display/OriginalLegacyFramebufferABI.h:14)、[实际参数](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/config/unixnu.toml:14)、[DarwinBoot ReadBootVideo](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p12/uefi-source/DarwinBootPkg/Driver/DarwinBoot.c:53)。

**关于 1080×1920 / 1920×1080：** 若原始矩形表达旋转后的内容，而后端只接受物理竖屏矩形，会触发 reason=3；这是一种可能机制。当前没有读到本机请求的这组值，不能认定已经发生宽高互换。贸然把驱动宽高改成横屏还会破坏 GOP/stride/mapping 一致性检查。

### D4. Copy 与内存安全边界

必须保留：[NativeLegacyRequest.h 32–51 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyRequest.h:32) 的非零尺寸、上限、stride≥width×4、乘法溢出、allocation/descriptor 长度、地址加法和目标对齐检查；还应保留 IOSurface/descriptor 类型检查及格式限制。

`kickOnce()` 会再次对真实 source 和实际 GOP 执行范围检查，再 map source、调用 copy-layout 检查并逐行复制。source stride 与 destination rowBytes 独立；因此不同 pitch 不需要也不应伪造为相同。复制/填充完成后有 store barrier，再进入帧完成路径。[复制入口](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:278)、[逐行复制](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p20-r11/framework/drivers/NativeMobileDisplay/NativeLegacyRequest.h:115)。

若扩展局部复制，应使用检查后的 64 位运算求每行起点与最后一个访问字节；不能仅先算一个可能溢出的 `x+width` 再比较。未知多平面/压缩、缩放、旋转和同步请求继续拒绝。当前 `&BGA` 的普通行假设是另一个实现范围约束，但它属于格式/复制层，不是最近 reason=3 的直接解释。

## E. 其他设备实际实现说明

### E1. Pagani：软件刷新和集成修复，不是已证明的矩形特判

已读历史成功提交中的 [Stage7Probe.cpp](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/Pagani/scripts/stage7/Stage7Probe.cpp:34)、[驱动编译脚本](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/Pagani/scripts/build-stage7-driver.py:16) 及 [实际集成补丁](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/Pagani/patches/native-mobile-display-stage7.patch:1)。

Stage7 检查真实 GOP、32bpp、无旋转，建立 IOPlatformDevice memory resource 和 display-subsystem；使用原始 Legacy 的 start/probe/PM/客户端。映射回调本身没有一套裁剪变换实现。补丁加入软件 vsync、刷新属性和队列推进等，不包含 validateSwap/rectOffsets 修改。

历史 README 明确说 Build 39 在更大内存/Data、软件刷新及同版用户态等一组改动后进入 UI，并未逐项归因。[成功记录及限制](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/Pagani/README.md:7)。因此答案是：**没有找到 Pagani 如何专门解决大量 reason=3 的公开源码证据，不能由成功显示倒推出它修改过这个条件。**

当前 format-2 [board.toml](https://github.com/IssacL04/UniXNU-Pagani_SM8750/blob/c27ed34c7c4d5192f18d851c1f879c01722a700f/board.toml) 仍选择 NativeMobileDisplay 和 Stage7Probe，发布 display-scale=2、rotation=0；[porting.toml](https://github.com/IssacL04/UniXNU-Pagani_SM8750/blob/c27ed34c7c4d5192f18d851c1f879c01722a700f/porting.toml) 记载 GOP 1216×2640、stride 4864，并明确迁移版本需另做实机核对。不能把该模式或内存窗口照搬到一加 5。

### E2. QEMU：相同目标版本的原始 API 验证，不是本机请求实值

设备 [board.toml 44–50 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/qemu/board.toml:44) 固定同一 kernelcache hash。实际 probe 在 `package/LegacyDisplayProbe.cpp`，不是这个设备仓库中的 `kits/qemu/` 路径；框架组装后路径才会变。

[拒绝记录代码](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/qemu/package/LegacyDisplayProbe.cpp:127) 与本机布局同源，仅做观察。上游 800×600 的原始 API 测试能支持一种 packed 布局和全帧请求的成功，不覆盖所有原始 CoreAnimation 请求。CPU/QuartzCore 软件渲染只是像素生产者路径；它不会自动把非全帧/有变换请求变成本机后端支持的请求。

### E3. sheng：scale 策略、原始协议正负测试与实际显示分开

sheng 的 package/LegacyDisplayProbe 同样保留拒绝字段；unit-scale policy 与本机探针同源，最新板级参数选择 scale=2。原始 scale 返回检查与负向 geometry 检查分别处理 mode 合法性和后端支持范围，没有发现“允许所有非零字段”或“删除矩形检查”的修复。

[普通原始 API 用例](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/sheng-current/package/OfflineLegacySwap.mm:54) 通过原始 SwapSetLayer 提交完整 CGRect，最后一个参数为 0；这只证明该用例使用零，不证明系统所有调用都为零。[负向用例](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p20/upstream/sheng-current/package/FramebufferGeometryDiagnostic.cpp:35) 正好展示了源目标 extent 可以不同。没有把 SM8550 的硬件寄存器、地址或 GPU 实现纳入本机修复建议。

### E4. ios-topaz：实际源码支持局部映射，但属于另一套 ABI

已实际读取固定提交中的 [0046-sweet-iosurface-iomfb.patch](https://github.com/MakrSas/ios-topaz/blob/e4772911e116ab4ab6c4ac1f38e6106fefe8b483/xnu/patches/0046-sweet-iosurface-iomfb.patch)。文件 1,449 行，SHA-256 `0f23986cf9861ad52a6a0986328fa83c9f0ec222d53dee2bce5921d79c90e5be`。这是读取源码，不是只读 README；本轮没有应用补丁。

| 文件内行号 | 实际行为 | 对本机的启示/边界 |
|---|---|---|
| 1008–1011 | 自有用户客户端 SwapEnd record 大小 0x46c；surface id 从 0x54，矩形从 0x64/0xc4 | 与本机原始家族内部 request+17/+81 完全不是同一层，不能换偏移 |
| 1128–1140 | 从 PE_Video 获得 panel 地址、宽高、rowBytes，建立 physical surface | 同样依赖真实 scanout，不仅按屏幕规格猜 pitch |
| 1231–1237 | 循环 enabled layers；分别读取 signed sx/sy、dx/dy 与目标 w/h | 显式区分 source 与 destination，非零原点不是天然非法 |
| 1260–1281 | 限制源/目标几何，检查实际 backing 长度 | 表明局部复制需要坐标和 descriptor 范围一起处理 |
| 1284–1288 | 按各自 stride 与 x/y 偏移逐行复制 | 不能只放宽 NativeLegacyDisplay 的 validator、继续使用原左上角全屏复制器 |

该早期实现对负坐标的归零、宽高回退等并非本机应直接复制的安全规范；它也不是通用缩放/旋转器。README/NOTES 的后续记录描述了 iPhone X 1125×2436 画布与 1080×2400 panel 的居中映射，以及再往后的直接 MDP scanout；未对整套后续补丁应用后的最终 AppleCLCD 逐行重建，不能以 0046 代表最新完整运行实现。这仍足以证明实际公开代码采用了与 full-frame-only 不同的几何处理策略，不能证明 iPadOS 17 当前请求与其 wire record 一致。

## F. 问题清单、优先级与最小修复建议

### F1. P1：完整画面限定与正常几何请求的兼容性缺口

- **位置/表达式：** NativeLegacyDisplay::validateSwap，428–434 行：两组 `rect[0/1]==0`，`rect[2/3]==displayWidth/Height`。
- **实际行为：** 有边界的偏移、裁剪、不同源目标尺寸也一律 reason=3；后续复制器不理解这些坐标。
- **证据等级：** 限制本身已证实；本机是否触发属高可信兼容性风险。原始生产/边界代码、geometry 负向用例和 topaz 实际坐标复制均支持它不是所有请求的 ABI 规则。
- **最小修改方向：** 若实值是无变换的等 extent、1:1 单层局部矩形，联合修改 validateSwap 与 kickOnce/复制辅助函数，生成已校验的 source/destination window；逐行分别加源/目标偏移。若只是首层完整 backing、请求另附 damage，要先区分 damage 与 layer 矩形语义。
- **风险：** 不正确的裁剪/清黑策略会破坏未更新区域；不同 extent 当 1:1 会画错；只删除比较会越过复制器的真实能力边界。

### F2. P1：812 dummy 被全零门槛覆盖，三个字段语义未分离

- **位置：** validateSwap 433 行和 blank 分支 457 行；probe 141–147 行只是保存原始 word。
- **实际行为：** 即使矩形完全符合，任意一个非零 word 都会返回 reason=3。原始家族对 dummy 非零存在完成路径处理。
- **证据等级：** 零值门槛及“Options”命名不完整已证实；本机是否发出非零 dummy/其他字段仍需实值。尚不能认定 current code 意外写错了一个常数。
- **最小修改方向：** 先给 804/808/812 分开命名/限定，不赋予未经解码的 flags 语义；若确认 dummy 触发，沿同版原始完成代码确定是否跳过像素但维持真正的队列/等待语义。只允许实际理解的分支。
- **风险：** 将 dummy 当普通帧复制，或伪造完成，会造成等待、代数和资源生命周期错误；以“原始处理过 15”为由把所有非零值放行不成立。

### F3. P2：mode/scale/实例的一致性没有本机原始返回值证据

- **位置：** startHardware 503–508 行、ABI header 16–18 行、probe 417–443 行、validateSwap 434 行。
- **实际行为：** 驱动把 GOP 像素宽高写入原始成员，动态发布 scale=1 / rotation=0；拒绝非零 PE_Video rotation。
- **证据等级：** 发布机制已证实；“当前一定发布错尺寸/scale”未证实。原始 request size 的同对象赋值使简单尺寸不一致假设降级。
- **最小修改方向：** 只有发现具体 source/destination 或原始 mode 返回对象不一致，才统一对应的 mode/scale/orientation contract；不得把设备分辨率直接翻转或照搬 Pagani 的 scale=2。
- **风险：** 改 mode 可能使用户空间重新创建不同尺寸 surfaces，扩大而非缩小不一致；GPU/CPU渲染坐标与实际 panel 坐标不能混为一谈。

### F4. P2：拒绝快照缺乏一致性/历史，现有日志不能唯一定位 reason=3

- **位置：** [recordRejectedSwap 135–152 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/source/LegacyDisplayProbe.cpp:135)、[validation observer 228–234 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/source/LegacyDisplayProbe.cpp:228)、[CompactObserver 119–125 行](/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/r2/source/CompactObserver.c:119)。
- **实际行为：** 每次 reject 覆盖同一组 volatile globals，无历史、无完整发布序号；reason 与其余数组顺次写，另一个观察线程可能看到跨请求组合；即使 cpus=1 也可能在可抢占点切换。显示页面只读计数和 reason，没有把 rect/options/size 展示或持久化。
- **额外限制：** `native_legacy_swap_request` 是最近一次验证请求，`native_legacy_rej_*` 是最近一次拒绝请求；不能在事后将二者无条件绑定。`native_legacy_rej_surface` 仅在 reason=4 的 surface 检查失败处更新，拿它解释最近 reason=3 可能读到旧值。
- **证据等级：** 已证实的诊断限制，不是已证实的显示拒绝根因。
- **最小建议：** 若最后必须补数据，只做同一次失败回调的一致快照并带 request/self 标识；不需要完整诊断框架。本轮不实施。

### F5. 已排除为“最近 reason=3 直接原因”的项目

没有发现当前矩形读取必然错位、endianness 颠倒、把 double 当 uint32、把 width 当右下角、stride 被写死为用户空间 pitch 的证据。descriptor 长度/格式/planes 等检查处于 reason=4，不是最近 reason=3 的直接失败项。队列 8-entry 验证表满后覆盖等其他后续风险，不能在只有 2 次成功传输的记录上冒充 1,587 次前置拒绝的原因。本轮不扩大到这些问题或 Root/UFS/APFS/Data/启动链。

## G. 是否具备直接修复的证据，以及最少缺失数据

**具备直接修正审计说明和字段命名的证据；尚不具备选择一个行为补丁并认定它能修复本机的证据。** 现在可以明确不应改 offset 17→16、81→80，也不应删验证、全部允许非零、绕过范围检查或先把宽高互换。这些“快速修复”没有同版二进制支持。

若选择扩展局部复制，必须联动修改的具体位置已经明确：`NativeLegacyDisplay::validateSwap()` 的 428–434 行、`kickOnce()` 的 278–362 行，以及 `NativeLegacyRequest.h` 的复制 layout/行偏移 helper；不能只改单个判断。若实值是 dummy/状态触发，应优先修对应请求的队列/完成处理，而非复制函数。

本轮在 `out/p21/r2` 已有 JSON/log/md/txt 和现有符号材料中未找到 rej_rects/options/size 的 live 值。内核中这些符号可定位，但文件中的零初始化/BSS 不是运行内存快照；不能把它们解析为本次请求全零。既有分析中提到 globals“已经捕获”只表示运行时写入机制存在，不表示这些值已经回传保存。

若所有现有证据仍不能补齐，唯一必要的第一层数据是**同一个 reason=3 请求**的：

1. A/B 两组四个 32 位原始值（8 个 word，保留 hex；坐标另按 signed 解读）。
2. 偏移 804、808、812 的原始值（3 个 word，812 标记 dummy，不先解释另外两个）。
3. 请求 816/820（2 个 word）和当次原始 getter 的 display width/height（2 个 word）。
4. 同一次记录的 self/request 标识和计数/代数关联，以确认这 15 个 word 来自同一请求而非 volatile globals 的混合快照。

这 15 个 word 可直接定位 reason=3 具体子条件。只有在其指向尺寸/映射问题后，才补当次 GOP mode、IOSurface width/height/stride/descriptor length 或原始 mode 返回值；无需先采全系统日志、全进程堆栈或再做性能压力测试。若怀疑同一请求重试，再比较少量相同 request/generation 的记录，不从累计拒绝数反推重试事实。本轮没有要求或执行新的实机采集。

## H. 对任务十三个问题的明确回答

| 问题 | 回答 |
|---|---|
| 1. 本地与官方 validateSwap 是否一致？ | 是。本轮读取的官方 main cf3b429 对应 C++ 文件与本地 SHA-256 完全一致，最终内核指令也确认该条件编入。 |
| 2. reason=3 字段读取符合原始 ABI 吗？ | 17/81、816/820 及 slot 296 调用由同版原始机器码独立支持；804/812 也有生产者。808 的完整语义、所有字段的通用允许值尚未闭合，不能说整个协议全部解码。 |
| 3. 有明显 offset/alignment/版本问题吗？ | 未发现；偏移不应因“不对齐”擅改成 16/80。用户 API double 与内部 packed int 是不同层。不能把该结论扩大成所有 ABI 均无问题。 |
| 4. 两个矩形必须全屏合理吗？ | 对当前仅支持左上角全屏复制的后端是必要能力门槛；不是原始 ABI 的普遍要求，存在兼容性缺口。 |
| 5. 三个 Options 必须零合理吗？ | 是保守支持策略，不是 Apple 非零必非法的证明；812 实为 dummy。未知语义继续拒绝，不能全部放行。 |
| 6. 有横竖屏/scale/尺寸风险吗？ | 有功能风险，但没证实本机请求宽高互换；request size 同对象赋值降低了单纯逻辑尺寸猜测的优先级。 |
| 7. Pagani/QEMU/sheng 已解决同类问题吗？ | 没找到公开的本机同类 reason=3 专项修复；成功 UI、完整原始 API 用例和 mode 验证不能替代失败请求的字段证据。 |
| 8. 当前 mode 发布符合成功实现吗？ | GOP→成员宽高→getter、无旋转、软件刷新机制相近；scale/分辨率是板级选择。缺本机原始 mode 返回对象，不能声明所有用户空间属性等价。 |
| 9. 1,587 次拒绝有静态共同原因吗？ | 当前严格条件可以解释某一类重复合法请求被持续拒绝；没有历史样本，不能证明本次数量对应同一类或同一个请求。 |
| 10. 最可能的前三项？ | 非全帧/非零原点/源目标几何；非零 dummy/其他未解码 word；mode/方向/scale/对象一致性。只有优先级，没有无数据支持的概率断言。 |
| 11. 足够直接改行为源码吗？ | 不足以选择本机修复分支；足够纠正字段解释，排除若干盲改。扩展后端属于明确功能工程，不是已定位的单行 bug 修复。 |
| 12. 若修，应改哪里？ | 几何问题须联动 validateSwap、kickOnce、copy helper；dummy 问题须按原始完成语义专门处理。不能只放松 gate。 |
| 13. 最少缺哪些动态字段？ | 同一失败请求的 8 个矩形 word、804/808/812、816/820、当次 display width/height，共 15 个 word，加 self/request 关联。 |

## I. 最终建议与交付范围

继续保留 P21-R2 既有镜像和全部证据。当前应把审查重点从“CPU 慢、是否再等几分钟”转到“原始请求进入了哪个未支持的几何或 dummy 子契约”。本轮没有发现一个足以无条件合并的上游修复，也没有把缺失的动态值编造出来。

**本轮交付仅为本报告。未实施修复，未生成 patch/candidate，未构建，未操作设备。**

