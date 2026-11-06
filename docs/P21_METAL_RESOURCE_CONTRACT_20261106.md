# A540 device／resource 契约集中适配 · 2026-11-06

## 上游与适用范围

官方固定 `16e592abee8a550dec8154ed1f38b5ebc4f16213` 的 `docs/porting/gpu.md` 74–104：真实提交、fence、缓存维护与退休必须先于设备发现／呈现成功宣称。sheng 的 A740／Turnip 硬件命令不适用于本板。

同SoC sagit `673f85070fe9f7872793015eb230a5c3987d4a8c`：
- `drivers/gpu/QcomMTL/QcomMTL.mm:1460–1531` 初始化使用真正 superclass init、独立 service retain、真实 registry／connection／capability gate，之后初始化 limits/features；其 iOS18 ABI、跨客户端提交和 compiler 不直接复制到21H461。
- `:1637–1640` 不支持的线性格式不能返回0对齐；本端未支持texture-buffer，对应查询明确抛拒绝，避免调用者用0算对齐。
- `:1734–1743` heap布局失败返回 `{0,0}`；本端所有heap/sparse/indirect/fence/depth/sampler/no-copy创建仍未实现，返回nil或zero-invalid布局，不创建假资源。
- `:2288–2320` protection/heap/alias/purge与实际backing一致；本端保留非heap、非aliasable、NonVolatile wired资源，非法／Volatile／Empty请求明确拒绝，不抛弃DMA或外部IOSurface内存。
- `:2610–2658` texture资源／parent／buffer／sparse／swizzle属性来自真实布局。本端无公开buffer view、无parent／root view，返回nil／0；texture view所有变体返回nil，不创建独立generation却共享backing的危险别名。

## 本批改变

1. 原始21H461剩余28个device/SPI入口逐一有真实类型与负能力行为；sharedMemorySize读取 `hw.memsize`，不是后端推荐预算。FP configs为当前无通用浮点能力契约的0，不等A540硅片没有FP运算。no-copy失败不调用deallocator、不修改输入或取得ownership。
2. 新增buffer／texture非heap/alias/protection/purge契约、texture parent/buffer/sparse/framebuffer/shareable/compression/swizzle属性和view明确拒绝。原buffer/texture不采用完整MTL协议，属性齐全不代表全面资源语义。
3. 标准Tracked资源options与原default支持相同实际串行session；拒绝untracked/unknown/cache-mode、非identity swizzle及lossy compression于任何BO分配/import之前。allowGPUOptimizedContents是优化许可，不要求实施优化；本端保持linear并报告NO。
4. pipeline maxTotalThreadsPerThreadgroup由过宽1024收束64，与本端唯一padding workload／device限制一致；vertex amplification普通count1与limits1一致，不支持多视点放大。
5. 分配在pending command时不进入transport；newBufferWithBytes分配与复制在同一session锁内，adapter委托此原子操作，避免在另线程submit后contents为null或DMA执行时写入。
6. 实机集中验收仍同genuineSpriteKit AIR四色/同importedtexture/工作区7BO45056B/最终7free6view；新run使用显式Tracked options并核资源属性与NonVolatile状态。原compute/privateprobe不变。

## 离线验收

- `tests/test_native540_device_api.py`：3 tests PASS，原始target协议清单比对28 encodings、实际ObjC ASan/UBSan拒绝行为、arm64产品编译；最新 `/Users/dinnerabc/unixnu-p21/native540/device-api-20261106T155322876938Z`。
- `tests/test_native540_graphics_objects.py`：3 tests PASS、27 modes/19退休故障，新增descriptor拒绝零分配、purge/alias/view无副作用、inflight callback拒绝分配，以及四色/旧compute/arm64回归；最新 `graphics-objects-20261106T155312789598Z`。
- 初始测试enum initializer／nonnull编译错误及semaphore夹具死锁保留日志；死锁测试已停止，未运行设备。修正为transport callback内递归锁检查，不放宽产品wait或安全锁。

## 候选和边界

`out/p21/p21-sagit-resource-contract-20261106T155155Z`，same225s/seed/ADT/DTB/NativeAdrenoABI/power；新plugin/trust/payload/实际EFI重新构建中。源pins及快照在 `logs/resource-contract-source-evidence.json`，不混入上一连续四色镜像结果。

## 2026-11-07 接续构建与实机结果

上一会话已构建的 kernel／plugin 与当前 sourcepins 一致，接续完成 prelink／panic-screen／payload／实际 UEFI 重编和三项 EFI pins。镜像 SHA `7d63a577c0dc8c6a74c7f2cdba674c05cf3e89ac1e0806e6626dfc4565ecc09a`，新 gate `gate-20261106T163232Z.json`；same225s/seed/697Bargs/ADT/DTB/power/wire，未复用旧绑定固件。

单次 boot **`0x3d7f4997`** 的日志 `out/p21/persistent-logs/20261106T163705Z`，window SHA `ddd48b4502199bd72c16ba9ad84774a56ac2578dcf2858a14c2ee5aab637ceb3`、34rounds/0errors。实际 `DP n468 missing0 type0`、四色 red/green/blue/red、7BO45056B复用、`OR free7 view6 proof1 left0`，DI/DC/DR/RG/RA/compute/SF/SR保持；GICACHE pub5/acq5/error0。241.295s观测Recovery，BCB32B预置与回读/其他保持，无host reboot assistance。2displaytransfers不是本轮用户桌面报告或GPU UI合成。

100 Python tests（包含实际ObjC ASan/UBSan及arm64产品编译）、9核心GPU模型及额外quiesce/importcache/direct/SMMU/历史bplist模型通过；真实private ZAP文件模型另在仓库cwd通过。失败的字体路径/include/link命令保留并修正，不算产品失败掩盖。冻结 `out/p21/gpu-cp/rounds/20261106T163806Z-sagit-resource-contract-four-color-pass`。

DP0只证明方法／签名表。继承的abstract factory、全屏BGRA、通用shader／texture sampler／blend、显示reader完成、长生命周期多客户端均未完成。principal initializer仍不发布system default device；不采用假协议或强行MTLAddDevice，不声称系统UI GPU合成／FPS完成。当前结果已更新 `HANDOFF.md`，用户要求获取日志后提交本轮，后续各轮分别提交，不push。
