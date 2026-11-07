# A540 Metal 桌面接入底座 · 2026-11-07

## 当前目标与结果边界

用户优先要求真实Metal接管卡顿桌面。本轮从 `a9ffb26` 干净基线开始，未改OS/seed/BoardRuntime/安全策略，未启动新的设备实验。当前只完成桌面路径所需的运行时compiler、通用原始库准入、真实系统draw编码与长期BO底座；**尚未接通系统MTL对象/queue或显示reader，不能宣称注册、桌面接管或帧率改善**。

## 上游与本机差异

固定 sagit `673f85070fe9f7872793015eb230a5c3987d4a8c`（checkout clean），Mesa24.3.4 `769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de`：

- QcomMTL::libraryIndex/loadLibrary 的拥有原bytes、逐AIR哈希、stage/alias筛选和lazycompile语义用于 `NativeMetal540DesktopLibrary`。不直接复制iOS18 `_MTLDevice` ABI或假conformance。
- QcomGPU/AdrenoPipeline.h 的原始draw直接用于 `NativeMetal540DesktopDraw`，经内部 typed参数连接本机BO/IOVA；不移植其MMIO或49-bit页表。当前拒绝depth/MSAA/feedback和顶点constantwindow，片元window由上游原plan/copy/scratch门禁处理，不改机器码/反射。
- QcomMTL的资源持有/失败隔离语义用于 `NativeMetal540DesktopSession`；本机kernel每个job持有全部mappedBO，因此新session同样在issued!=completed时阻止任何分配/释放/CPU视图重用。实际BoFree成功才释放slot/IOVA，失败stickyhold；不是手动绕kernelidle/unmap/TLB。
- 上游 NativeConnection 使用自己的Family selectors/semanticshader协议，不能直接用于本机NativeAdrenoABI；本轮没有向kernel复制这些selector或放宽单clientgate。剩余主要适配点就是将上游完整MTL对象链绑定到本机session/draw/library。

## 原始目标输入与运行时

旧临时metallib目录和21H461卷在执行器重连后消失，当前SDK无libLLVM.tbd。已核对本机保留OS/SystemCryptex镜像原SHA并只读挂载：

- OS `3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d`。
- SystemCryptex `b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f`。
- 实际QuartzCore default.metallib9670832B、167AIRentries；maincacheUUID `590d839a82d230f1a17d0cc053420f73`。
- 原始 `/usr/lib/libLLVM.dylib` UUID `0953f8c09b29364ea509ba77eed900d4`。新增只读auditor检查原exporttrie；旧hostbackend220LLVM C imports全部有export。仅生成链接声明，不复制Applebinary，不将export存在当C enum语义/设备加载证明。
- `p21-build-metal-runtime-compiler.py` 在新local树构建arm64-ios17.7后端，链接actualiOSLLVM，禁止hostLLVM路径。成功targetSHA `3c8b18bf0cbf61401ddccfcdc65b1a683f85c45b87f7ad1a532d084e943db8db`；actualtargetimports另经原cacheaudit。初始danglingREADMEsymlink与crossfile缺objc失败保存，修正后buildPASS。

本地证据 `/Users/dinnerabc/unixnu-p21/native540/desktop-metal-20261107T031916Z`；NAS副本 **`out/p21/p21-metal-desktop-bridge-20261107T034422Z`** 保存本轮源码pins/runtime/log/manifest。103项完整回归PASS，最终alias/binding拒绝后桌面3tests另PASS。所有Apple输入/产物/log保持本机，不提交。

## 新代码与验收

- `NativeMetal540DesktopLibrary.{h,cpp}`：upstreamcontainerparse、immutableownedbytes、全modulehash、typedstage、weakimagecache、未知/歧义拒绝、compiledregistry/stagevalid。
- `NativeMetal540DesktopSession.{h,cpp}`：64slots/实际querymaxBO/128MiBbudget、单inflight、fullHDpageallocation、generationID、低地址空洞firstfit复用、CPUview/cache、真实ACK后free。failedalloc/submit/wait/free不自动clear/free/reconnect。
- `NativeMetal540DesktopDraw.{h,cpp}`：IB与scratch独立、真实绑定长度/地址校验、原始上游全draw、sampling/blend/MRT/BGRA布局与constantwindow计划。硬件命令来自fixedsource，未引入A740/另板地址；软件验证不等实际GPU执行。
- `test_native540_desktop.py`：actualObj-independent ASan/UBSan、2000BOreuse/fullHD/失败与过期ID、原始QuartzCore image_rect_blit VS/FS懒编译+哈希拒绝、双BGRA1080×1920 pitch4352、sample3/linear/premultipliedblend/MRT完整PM4、欠分配拒绝、arm64产品编译。未CPU生成/复制桌面图像、没有dummydevice。

## 下一轮可直接接续

1. 将QcomMTL的长期资源/queue/program/shmem/notification/presentation行为绑定到现有session/draw/library（上游Family selector不能直接使用）。补实际21H461SPI/继承abstract语义，requiredmethods/types通过还不等行为闭包。
2. 提供typedMetalresource/commandqueue/renderencoder/纹理sampler和descriptor/constant映射，优先QuartzCore167函数中的真实请求；需要specialization时保留actualfunctionconstants，不以默认值代替。
3. 对照actualruntimeLLVM C opcode/typeenum与AIRRuntime的兼容路径；targetdylib仅build/export通过，仍需设备加载/实际runtimecompile证明。
4. backboardd独占桌面模式必须和mediaserverd旧probe互斥，principal只在真实闭包通过后返回device，原factory自然注册；不直接MTLAddDevice/fakeprotocol/Applefamily。
5. fullBGRA IOSurface→GPUfence→原displayconsumer→reader结束后复用；NativeLegacyDisplay early-retire不得让producer先覆盖源帧。GPU复制CPU合成桌面不是Metal合成。
6. 完整offlinegate后才onecandidate/onefastbootboot，recovery新日志回收、freeze、HANDOFF与本轮commit；没有闭包则不boot明知会失败的注册候选。
