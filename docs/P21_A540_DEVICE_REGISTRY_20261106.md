# A540 真实 accelerator registry 与标准资源输入 · 2026-11-06

## 前置结果和适配来源

前轮boot0x3d7eac9d已证明actual_MTLDevice初始化、资源deviceidentity、completioncallback1和正确RG像素；DP对目标MTLDevice/SPI共468required方法记录37missing/1type。原cacheprotocol完整静态清单在local `native540/protocol-audit-21h461-20261106T1223/protocols.json`，不靠循环试启动定位。

优先参考固定sagit `QcomMTL.mm::initWithService`（673f85070fe9f7872793015eb230a5c3987d4a8c）：service独立retain、IORegistryEntryGetRegistryEntryID、真实memorybounds/allocator、deviceidentity；不套其22H374私有transport或capabilityvalues。

本批补：

- adapter输入真实service，IOObjectRetain/registry读取成功才生成devicecontext，析构只释放自己的service引用。registryID和acceleratorPort不伪造、不复用另板ID。
- hasUnifiedMemory、non-headless/non-removable及sampleCount1按本机共享DRAM和受限backend回答；不是完整Metalfamily声明。recommendedworkingSet为当前64×16KiB预算，不宣称实际已分配容量。
- newBufferWithBytes按当前max16KiB/Shared严格分配后copy原输入，typedBuffer length/cache/owner/提交依旧验证；nullptr拒绝、未知option拒绝。不是CPU图像输出，actualvertex/color输入经新接口进入GPU。
- supportsFamily协议要求unsigned NSUInteger，目标base旧实现q，adapter按公开protocolQ正确实现；family仍全NO。
- DP保持只读actualrequiredsignatureaudit；DR独立记录actual/expectedregistrymatch，DCidentity/callback/allocated与RGpixel分开。

## Host验证与候选

graphics/原compute/callback/arm643testsPASS，实际newBuffercopy内容一致；nullable空输入测试最初触发-Wnonnull，修正头声明后运行仍拒绝空指针。adapterarm64compilePASS。未实现heap/sparse/indirect/sampler/depth/IOSurfaceobject接口，不用dummy返回伪完整协议。

候选 `out/p21/p21-sagit-device-registry-20261106T123058Z`，sameSeed/697Bargs/ADT/DTB/power/wire，localkernel/plugin/trust/payload重建，EFI/pins/gate进行中，尚未本批device验证。下一检查DRmatch1、DPtype0/missing减少、DCidentity1callback1、RGbad0和原回归。没有systemdefaultMetalregistered或presentation/UI合成声明。
