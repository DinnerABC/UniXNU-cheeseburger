# Native540 标准创建与对象契约 · 2026-11-07

## 来源与边界

固定同SoC sagit `673f85070fe9f7872793015eb230a5c3987d4a8c`，`drivers/gpu/QcomMTL/QcomMTL.mm:1749–1799,1801–1837,1859–1899`：file/default/URL统一库准入、descriptor内容检查、先复制异步输入、真实owner保活、一次completion。复用该语义，不搬iOS18私有ABI、通用compiler、Apple family或A740命令。目标本机21H461协议清单固定在本地protocol audit；新增14个factory selector逐项验证encoding，原28项保持。

## 本批实现

- `NativeMetal540Objects`／adapter提供 library file/URL/default、source同步与异步拒绝、compute function/options/descriptor及render options/异步入口。只有固定MLCompute／SpriteKit原始metallib哈希和已有不可变compiledregistry能成功；无runtime source编译、假reflection或假pipeline。
- file路径通过同一fd读取：localURL、O_NOFOLLOW/CLOEXEC/NONBLOCK、fstat普通文件与两种exact大小、有界read/EINTR、读取后大小/身份复核。符号链接／FIFO拒绝；改变或不匹配内容仍由实际哈希拒绝。不是Apple输入发布。
- options只支持None；unsupported options返回nil/error并清reflection。compute descriptor仅固定64线程、空stage/link、无archive/preload/indirect、default mutability；render同样拒绝不支持archive/preload/indirect/多视图。拒绝前不分配BO、不提交。
- async复制descriptor并强持有function／backend；adapter completion额外捕获真正publicdevice，防weak identity在调用者提前释放后丢失。handler在全局worker／锁外调用，支持重入；每次有handler调用恰一次，不将失败当Completed。
- Function/Library label按copy；fixedfunction无stage attributes/tessellation/specialization，argument encoder返回nil；compute静态shared长度来自compiledInfo，maxThreads64；pipeline label来自创建快照，imageblock查询明确拒绝；render仅amplificationcount1。不是完整协议conformance。
- 实机run走新URL＋sync options＋async pipeline，保持原同importtarget四色／7BO45056B／完整退休回归。持久 `DC … fc1 cb1` 单独记录创建callback，不与commandcallback混称。

## 离线证据

100 tests PASS（实际ObjC ASan/UBSan、arm64、原compute/render/AIR/surface/retirement）；local `native540/factory-batch-20261106T165415Z`。后续最终read-NONBLOCK与fc持久调用的graphics/oldcompute/arm643tests另通过，`graphics-objects-20261106T165650274833Z`。新增42项目标encoding（旧28＋新14）。异步复制后修改原descriptor、callback重入、unsupported、localURL/原hash、symlink/FIFO无阻塞均覆盖。初始缩进/缺getter编译与夹具error遗留/禁止nil computeFunction失败日志保留并修正。

## 候选与待验收

`out/p21/p21-sagit-factory-contract-20261106T165722Z`，BoardRuntime未变，复用前一轮已verified最终kernel并重新核对native/build/BoardRuntime；明确不是新XNU编译。新plugin/trust/payload/actualUEFI重建，same225s/seed/697Bargs/ADT/DTB/power/wire。sourcepins在preparation.json，旧成功不归给新源码。

## 实机结果与仍未完成项

candidate已完整构建／actualEFI3pins／gate `20261106T170019Z`，镜像 SHA `5a6c83c02d9db0ad2fc4edec26410ad5df7f674b332392ff61f4c7593383401d`。单次boot **`0x3d869cc1`**，raw log `out/p21/persistent-logs/20261106T170445Z`，SHA `457b7ecd37a1027dd5315111d5fb0ded162ca803fcce83e6b1860b368ad58290`，34rounds/0errors。**DC identity1 callback1 allocated45056 fc1 cb1**：实际initializedadapter的URL／sync／async pipeline通过，随后四色正确、7BO45056B复用、ORfree7view6proof1left0，DP0与旧AIR/compute/SF/SR/cache5/5/errors0保持。239.582sRecoveryreturn、BCB32B预置/回读其他保持、无hostreset帮助。冻结 `out/p21/gpu-cp/rounds/20261106T170538Z-sagit-metal-factory-four-color-pass`。

最终源码100tests PASS，candidate/logs/all-tests-final.log；其后仅补测试：async失败callbackonce、nilhandler、defaultlibraryFileNotFound另PASS3，生产142sourcepins不变，post-build-tests-only.json保存归属。6displaytransfers不是本轮用户桌面报告或GPUUI合成。

principal仍返回nil，无systemdefaultregistration。长期queue容量/跨帧资源池、继承abstract全分类、generalshader/sampling/blending、fullBGRA/display-reader/presentation和系统UI合成仍未完成，不虚报FPS。第一批已提交fb08a2f；本第二批在新boot回收／冻结／更新交接后单独提交，不push。
