# A540 桌面 Metal 对象接线 · 2026-11-07

## 已实施

从 `d41e085` 的 DesktopLibrary/Session/Draw/compiler 底座继续，新增 `NativeMetal540DesktopObjects.{h,mm}` 与 `DesktopTransport.mm`。这是一条具体可执行的 native selector 对象链，不宣称完整MTL协议或系统注册。

- 实际原QuartzCore库→lazycompiledfunction→pipeline、二维RGBA/BGRA/全屏pitch纹理、sampler、queue/capacity、commands、renderpass/clear、buffer/inlinebytes/texture/sampler/viewport/scissor/draw→原A540 PM4→现NativeAdreno submit/fence。
- 真实IOKit服务借用retain、0x4147单ABIclient、MapMemory/UnmapMemory、nativequery/BO/cache/retirement。当前CPUinhibit BO契约沿kernel不变，importedIOSurface由kernelphysicalcache维护；不新建Familyselector或伪硬件信息。
- descriptor复制后准入；原AIRstage/link/vertexlayout/bindingfootprint拒错误，sampler只当前linear/nearest clamp normalized、blend只预乘One/OneMinusSrcAlpha。小于硬件clear准入的区域仍拒绝，不伪CPU渲染。
- commit同步准备IB/scratch并冻结CPU访问，worker仅encode/submit/wait；所有commands/operation/resources保持owner。nested/latecallbackonce、callback内wait不deadlock、queue槽按completion回收。未持有的资源在inflight销毁进入deferred退休，真fence后再BoFree；失败保留owners，不close/free假完成。
- actualnativeGPUclear路径由sagit原PipelineEncoder::clearRows提供，新增DesktopDraw::encodeClear；不会把CPU桌面copy叫Metal合成。

## 来源与实际差异

固定sagit `673f85070fe9f7872793015eb230a5c3987d4a8c`：QcomMTL.mm 2130–2270 的buffer/import/owner、4064–4071 publish-before-admit、5608–5780 viewport/descriptor/typeddraw/liveness语义。A540原PM4编码不搬其他板地址；本机仍NativeAdrenoABI，没有sagitFamilytransport。对象state来自本机context和fixedcompiledreflection，不能copyiOS18 `_MTLDevice` ABI。

关键文件 `NativeMetal540DesktopObjects.mm`、`NativeMetal540DesktopTransport.mm`、`NativeMetal540DesktopDraw.cpp`；builder `scripts/p21-build-metal-desktop-objects.py`。

## 验证与证据

actualObjCASan/UBSan：原systemimage_rect_blit VS/FS/fullHD1080×1920双BGRA/pitch4352、sampler/blend/3实际submitwait调用、failurewait保留、inflightcontents拒绝、capacity1、nestedcallbacks、标准顶点layout/错误stride。transport是host模型，不是GPU执行；没有CPUexpected写图冒充设备结果。

初始nullable/forward-declaration/夹具owner stack-use-after-scope和不匹配原VS/FS varying失败保留并修正；未关ASan或放宽link。最后desktop4testsPASS、完整104testsPASS；产品arm64-ios17.7native对象dylib+真实IOKittransport、compiler链接和codesign strict通过。构建cache/output每次独立，不覆盖失败证据。

本地证据 `native540/desktop-objects-20261107T035509Z`，NAS独立 **`out/p21/p21-metal-desktop-objects-20261107T042638Z`** 保存sourcepins/snapshot/product/log。最终产品 `product-final-05`（审阅修复后重建），SHA `05c618a3d71ed1059db7acc70c8f56f605ad4cd55ab2bef154413fbf530a664c`，最终104回归见 `all-tests-final-05.log`、actualObjC/arm64见 `tests-final-05.log`。独立审阅发现并修复回调等待另一command的串行worker死锁、IOSurface读锁失败静默返回及lost标记可能撤销；回调移到独立globalexecutor、错误明确抛出、lost单向保持。cross-commandwait回归通过。IOSurface实际lock失败注入尚未做，不将静态修复当实机验证。

## 仍未完成与下一步骤

1. 接 `NativeMetal540DeviceAdapter` 的实际21H461 `_MTLDevice`身份到CBMetalDevice backend；现principal仍nil。必须对原runtime的MTLDevice/MTLDeviceSPI及resource/function/pipeline/command所需selector与行为逐项资格核对，不fakeprotocol或直接MTLAddDevice。
2. 本nativechain尚没有functionconstant specialization、compute/blit/fence/event、通用textureview/mipmap/其他格式及MSAA等系统实际负载。先真实backboardd/QuartzCore调用闭包，不补无关ray/mesh/sparse能力。
3. 设备runtimeLLVM C enum/actualload/编译尚需实机证明。当前iOScompiler构建+exportgate不是runtime成功。
4. drawable/present/reader-fence与原显示consumer未接；IOSurfaceImport可执行不意味着生产者可提前覆盖reader。须GPUdone→consumer→readerdone→复用，不GPUcopyCPUframe假桌面。

本轮不启动不完整systemregistration候选，最近真实device仍boot3d869cc1的此前成功log；当前设备未重新查询。按用户每轮提交规则保存本轮实质实现与离线结果，不push/刷分区，旧镜像/原库/私有logs保持本机。
