# P21 A540 IOSurface 接入前提 · 2026-11-06

## 当前结论

本轮 NEWBO 64×64 真正 VS/FS 离屏三角形已实机通过；修复候选boot3e2b9cf1同时恢复随后 owned metallib 对象链，详 `P21_A540_RENDER_PORT_20261106.md` 与 `P21_A540_OBJECT_PREPARATION_20261106.md`。可以进入私有 IOSurface 的受控导入/光栅写入阶段，但本轮没有实施这条路径。系统 Metal device和显示合成都未完成。

研究只使用本地固定开源实现与公开 SDK：official framework `16e592abee8a550dec8154ed1f38b5ebc4f16213`、sagit `673f85070fe9f7872793015eb230a5c3987d4a8c`、uefi-sheng `0eb4514db0744b8136a3897ab1af93b713c2c27b`。本机地址、21H461 ABI不借用其他板。

## 已有本机契约与缺口

- `NativeAdrenoABI.h` BoImport selector16，16B `{address,bytes}`，4KiB对齐；是calling-task虚拟页面导入，不是IOSurfaceID查找。CPU mapping/cache归caller，import无CPU-map token。
- `CheeseburgerGpuService.cpp::boImport` 使用calling-task `withAddressRange/prepare(kIODirectionInOut)`，单BO最大16MiB。job持有connection全部mapped BO，而非仅submit里的IB。
- 内核只保存prepared descriptor，不持有IOSurfaceRef/usecount/nativeaccessfence。用户态必须持有surface/mapping/context，失败不能作用域结束就释放、复用或补读。
- 自动cache维护只处理`b.buf`，import只有`b.desc`而被跳过。现有plugin的`dsb sy`仅适用于inhibit-cache ordering，不是cached IOSurface清理/失效。
- batch SMMU路径实际固定Read|Write、writeback0、shareability2；wire的请求permissions/writeback/shareability没有完整透传。现有shader“RO”是请求，不是已经验证的实际PTE保护。
- imported descriptor的`complete`返回值未受检查，尚无“complete失败保留owner”的完整证明。该差异必须修复并测试后才扩大import生命周期声明。

## 优先复用的上游

1. official `drivers/AdrenoMetal/NativeBridgeMetalFrontend.mm`：1369–1386 no-copy导入，验证base/allocation/planeoffset，CFRetain；175–191 releasecallback才CFRelease；270–286按准确address+length做sys_dcache_flush；921–959提交/真实完成cachephase与资源持有。首个probe不需导入缓存或多surface池。
2. official `drivers/Adreno740/NativeAdrenoMemory.cpp`：163–224 persistent VM ownership、完整长度/物理页准入；288–319 imported physical spans的publish/acquire维护。本机尚未移植，A740 PM4不可复制。
3. sagit `GPUSurfaceProtocol.h:21–51`：区分pixeloffset、imageBytes、DMAspan和rounded backing，不授予padding访问。
4. sagit `QcomGPUNative.cpp:120–150,196–253,409–459`：task授权surface查找、prepared descriptor、usecount、合并读写nativeaccessfence和完成发布；`QcomGPUBuffer.cpp:102–109`对原descriptor flush避免冲突alias；`QcomGPUHardware.cpp:716–747`真实完成后coherence再发布。这些是选定Family ABI契约，不是本机21H461直接可复制的私有布局。
5. uefi-sheng提供boot/GOP/RAM发布，不提供runtimeIOSurfaceimport；其3048×2032或framebuffer地址不是本板输入。

## 最小实现边界

- 一个fresh、未发布IOSurface，RGBA FourCC、plane0、64×64、pitch256、4B element；以actual getters验证base4KiB对齐、image/alloc恰16384B。遇到prefix/differentpitch/rounding首版拒绝，不align-down。
- IB/VS/FS/scratch仍NEWBO，只target BoImport到独立IOVA，不调用target BoCpuMap/mapView；原fixedfixture和encoder保留，oneSubmit onlyIB。
- CPU lock内初始化sentinel并unlock，GPU前对准确imported range用公开`sys_dcache_flush`；真实fence后readlock＋flush再oracle。锁/解锁返回值检查，无并行CPU写；timeout不readback、不补flush、不释放/重用。
- surface/CPU mapping/session持续持有至确认retirement。首版不共享、pool、crossprocess、CAMetalLayer或systemfactory。host测试非对齐/长度/pitch/plane、import/map失败、fence但无像素、timeoutowner保留，再新候选单轮boot。
- SDK `IOSurfaceRef.h`提供Create、Lock/Unlock与getters，`OSCacheControl.h`提供`sys_dcache_flush`。SDK存在不是21H461实际运行/cache效果的证明，锁/usecount也不等于GPUfence。

## 不混入显示验收

本机`NativeLegacyDisplay.cpp`当前接受BGRA和全屏尺寸，RGBA64×64不能直接scanout。`NativeGOPWire.h`是finished-frame transfer与sequencecompletion，不是小纹理compositor。现有surface GPUhook是2Dcopy+marker，不是triangle；其cachealias不能当cachedimport模板。

独立验收依次为：私有IOSurface真实raster像素 → 格式/全屏geometry/readerfence/swapretention → 实际显示消费。不得将CPfence伪装vblank或把本轮offscreentriangle称系统GPU合成。
