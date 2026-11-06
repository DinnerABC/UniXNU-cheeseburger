# 连续Metal AIR图形与资源复用集中适配 · 2026-11-06

本批按用户要求成批解决，一张候选集中验收，不为单个方法/诊断重启。固定sagit673f8507 QcomMTL提交/依赖语义和QcomGPUHardware::finishJob的真正完成/coherence/endGPUUse先于资源复用原则，不套其iOS18私有ABI或硬件地址。

## 功能

- 同一sharedIOSurface/真实SpriteKitSolidColorAIR对象链，按red→green→blue→red四帧提交与fullinside/outsideoracle；只上传输入颜色，不CPU写expected图像。
- texturegeneration防预先编码旧command写新代，inflight/failed保留submitted占用；成功fence/cache/timestamp后executionRetired才可复用。
- 每session一个IB/VS/FS/scratchworkspace，仅前一执行真实完成且无pending时复用；workspace存纯identityrecord而非强资源对象，避免session→buffer→sessionowner循环。实际allocation维持45056B/7BO，循环不新分配。
- 连续fence仍严格递增；host模型初始fixedfence17触发正确拒绝，修为真实monotonic模拟，不放宽NativeMetal540Client规则。
- buffer/texture标准identity/geometry/storage/cache/hazard/allocated/IOSurface/label属性对应已实现backing。inflight/lost/retiring CPU contents拒绝，oldCompute错误不读CPU目标。属性不声明fullMTLprotocol、CPUCaching全面一致性或systemdevice。
- 四帧后真实session7BO/6CPUviews/19callbacksretireProof、allocated0，原五BOprobe保持。RC/RX汇总帧数/恒定allocation/实际pixels，OR独立回收结果。

## 离线与候选

actualObjC ASanUBSan/oldcompute/arm643tests、27modes含19retirefailures、四帧无新allocation/generation冲突通过；oldretire7/Surface5继续PASS。成功pathCPUallocationcache真实managed不伪fence；失败仍保留owner。

新候选 `out/p21/p21-sagit-continuous-metal-20261106T151639Z`同225s/seed/ADT/DTB/PM4/power/wire，localXNU/plugin/trust/payload/UEFI及actualEFI3pins/gate通过后只一次临时启动。

## 实机结果

boot `0x3d7c55c7`，日志 `out/p21/persistent-logs/20261106T152547Z`，rawSHA `4338b3472d83f707011aa3a7fc166c4717f12764e54eef6f3e1ef9cae9509234`，34rounds/0errors。RC frames4/reuse1/45056、RX ff0000ff/ff00ff00/ffff0000/ff0000ff、OR rc0/free7/view6/proof1/left0均实测。GICACHEpub5/acq5/163840B/errors0，原compute/AIR/DI/RG/privateimport保持。241.164s观测rootRecovery，BCB预置、无hostreboot帮助；不是FPS/并发/系统UI合成测量。冻结 `out/p21/gpu-cp/rounds/20261106T152721Z-sagit-continuous-four-color-metal-pass`。

后续device/resource契约批次的源文件已变化，不能把本轮镜像成功归给后续源码；本轮sourcepins和artifact保持不变。
