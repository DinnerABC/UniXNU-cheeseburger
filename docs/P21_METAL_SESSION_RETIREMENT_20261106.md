# Metal对象session逐资源退休 · 2026-11-06

## 目标与上游

用户确认450秒观察轮boot0x3ccb2dfc正常进入桌面，并要求后续deadline取日志首帧后十几秒。firstpaintkernel209.709s，所以本轮225s与真实Metal资源生命周期改动合并；不为timer独立boot，不继续壁纸或其他硬件。

固定sagitQcomGPUBuffer::free149–178及官方NativeAdrenoUserTest170–177：preparedDMA/unmap/complete错误不释放，CPUviewunmap→BoCpuUnmap→BoFree回执。当前kernelBoFree已检查jobholds/cachepending/idle/SMMU/TLB/importcomplete，userlandack是资源身份准确的真正证明，close/globalcount不是。

## 适配

RetirementLedger模板固定capacity默认5(保持旧probe)，session64BO/192callback预检。ownedgraphics实际7BO：importtarget1/ownedviews6→19callbacks。仅完成命令/真实fencecache成功、无pendingcommands、无lost才允许explicitretire；前面资源分配失败或不确定reply禁止证明。

sessionledger记录handle/iova/cpu/token/bytes/imported，序列化客户端call；回收开始不再allocation或submit，成功proof后清records/owners/allocatedbytes，existingBuffer contents/texturegetBytes不可读，原对象不能指向复用后的内存。任何partialfailure保留session/owners，首错sticky、不重free、不close触发再次complete；callback预算不能preempt内核call，硬件实时deadline仍不保证。

GraphicsOwnedRun仅pixelcorrect后retire；独立OR持久行rc/free/view/proof/currentallocated。在同real_MTLDevice实现中委托backendsession，保持原AIR/pipeline/pass/texture/CBfence/readback、原五BOprobe/SF/SR回归。systemdefaultdevice/protocolfull/跨帧presentation未宣称。

## Host与候选

graphicsactualObjCASanUBSan/oldcompute/arm643testsPASS，七资源成功free7/views6(或全owned7)与19failurepoints、旧view拒绝/no retry/retirebusy验证；oldretirement7/Surface5/capabilities4/heartbeat12/evidence17回归通过。失败初始化ObjCivar语法、额外不存在callbackpoint记录保存修正。

候选 `out/p21/p21-sagit-session-retire225-20261106T143918Z`完整localXNU/plugin/trust/payload/EFI通过，同seed/ADT/DTB/power/wire、bootargs450→225。gate20261106T144356ZactualEFI3pins，image `6d46f6cbff58d40c24e3152f2677806065aa42be42654dfd373946a7bdd94bfe`。

## 实机结果

singleboot `logs/boot-once-20261106T144445Z`，240.9348s观测rootRecovery，BCB预置/其他字节回读保持、无hosthelp。新boot `0x3d701cdc` log `out/p21/persistent-logs/20261106T144855Z` rawSHA `ab9140460da7cfdab7f3742c01a5ed5a54bf3f7553e06fee47ae17e55e9243df` 34rounds/0errors。

**OR rc0 free7 view6 proof1 left0**，providerfree12=ownedgraphics7+旧privateprobe5；实际7BO/6CPUviews/19callbacks与kernelSMMU/complete安全回执通过。RGpixelbad0、原AIR/compute/DI/DC/DR/IOSurface/SR回归全保持。2displaytransfer，225秒本boot未取得独立用户桌面确认，prior450桌面user确认只作deadline依据，不复制成本boot光学证据。

冻 `out/p21/gpu-cp/rounds/20261106T145005Z-sagit-metal-session-retire225-pass`，device-result.json/sourcepins/gate/raw归属保持。fullsessionretire不是per-frame资源重用池，系统Metal注册/fullMTLResource/SPI与实际GPUUI合成仍未完成。
