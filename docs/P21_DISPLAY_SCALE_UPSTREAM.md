# P21-R3：源目标尺寸不一致的上游案例与修复方向

日期：2026-10-25。本轮为只读上游查询及修复方案，不修改驱动、不构建、不启动设备。新增 R3 用户转录记录，原有镜像和审查报告保留。

## 1. 当前输入与结论边界

用户报告首个和最近一次 reason=3 样本均为 SRC `(0,0,1242,2286)`，DST `(0,0,1080,1920)`；请求、驱动、GOP 均为 1080×1920，804/808/dummy 均为零。FIRST mask=000c 正好对应源宽、高不等于驱动宽、高。

LATEST 转录为 600c，与列出的字段不一致，且包含当前 mask 生成函数没有定义的 0x4000 位，不能忽略或解释成已知选项。需要原图核对；当前不把它改写成 000c。首个样本和重复的 SRC/DST 字段已指出具体兼容性缺口，但没有证明历史 1,681 次拒绝全部相同。

两矩形不同且非等比例：1242/1080=1.15，2286/1920=1.190625。按已经核实的原始源/目标矩形语义，这属于后端需要处理的映射；不能用统一 display-scale、交换屏幕宽高或直接裁掉右/下部分代替。

## 2. 官方 NativeMobileDisplay

本轮重新查询官方 main：`cf3b42979a9c44219e7bb149543e6fc3388b58ed`。其 [NativeLegacyDisplay.cpp](https://github.com/UniXNU/NativeMobileDisplay/blob/cf3b42979a9c44219e7bb149543e6fc3388b58ed/NativeLegacyDisplay.cpp#L413-L464) 仍要求源、目标矩形都为完整显示尺寸，没有现成的任意比例缩放分支。[NativeLegacyRequest.h](https://github.com/UniXNU/NativeMobileDisplay/blob/cf3b42979a9c44219e7bb149543e6fc3388b58ed/NativeLegacyRequest.h#L115-L145) 的 copy helper 也只有一组 width/height，是逐像素对应复制。

因此不存在一个本轮找到的“同步官方新 commit 即解决”的补丁。不能将 validateSwap 条件放宽后继续使用旧 copy helper：那会从源左上角复制 1080×1920，而不是将 1242×2286 映射到完整目标。

## 3. ios-topaz / ios-sweet：实际案例是居中裁剪

实际阅读固定 topaz commit `e4772911e116ab4ab6c4ac1f38e6106fefe8b483` 中的以下源码补丁：

- [0061-sweet-iphone-x-display-size.patch](https://github.com/MakrSas/ios-topaz/blob/e4772911e116ab4ab6c4ac1f38e6106fefe8b483/xnu/patches/0061-sweet-iphone-x-display-size.patch)：对用户空间发布 1125×2436 画布，保留真实 1080×2400 panel；用 fOffX/fOffY 将逻辑坐标移至 panel，并调整源坐标及裁剪范围。
- [0114-topaz-iphonex-canvas-aligned.patch](https://github.com/MakrSas/ios-topaz/blob/e4772911e116ab4ab6c4ac1f38e6106fefe8b483/xnu/patches/0114-topaz-iphonex-canvas-aligned.patch)：水平 panel window 向下对齐至 4 像素/16 字节，处理其 MDP 直接 scanout 要求。
- [0948-topaz-display-modes-60hz.patch](https://github.com/MakrSas/ios-topaz/blob/e4772911e116ab4ab6c4ac1f38e6106fefe8b483/xnu/patches/0948-topaz-display-modes-60hz.patch)：发布 frame-clock 使用的模式信息，针对其 50ms pacing；没有给本机提供缩放复制器。

0061 是一比一取逻辑画布中间的窗口，不是把完整源画面重采样成 panel 尺寸。0114 的 16 字节对齐来自其 MDP，不能作为 MSM8998/GOP 的参数复制。本机明确收到不同 extent 的 SRC→DST 请求，不能因另一个移植成功就改成居中裁剪。

本轮 API 还定位到 ios-sweet main `45263bee51df4a0f55cdf9c649af886399488ddb` 的同名 0061 文件。以上参考是另一套 iOS 16 SweetSurface/AppleCLCD 的软件策略，不能替换本机 21H461 原始内部 request 布局。

## 4. 可复用的 CPU 缩放实现：libyuv

本轮读取官方 Chromium libyuv，main commit `47ba8e2160b71a7cf95c0720cd8944ab771e533e`：

- [scale_argb.h](https://chromium.googlesource.com/libyuv/libyuv/+/47ba8e2160b71a7cf95c0720cd8944ab771e533e/include/libyuv/scale_argb.h)：ARGBScale 接受独立的源/目标宽高、stride 和过滤模式，另有 ARGBScaleClip。
- [scale_argb.cc](https://chromium.googlesource.com/libyuv/libyuv/+/47ba8e2160b71a7cf95c0720cd8944ab771e533e/source/scale_argb.cc)：ScaleARGBSimple 为无插值定点路径，水平和垂直分别推进，支持任意比例；另外有双线性路径。
- [scale_common.cc](https://chromium.googlesource.com/libyuv/libyuv/+/47ba8e2160b71a7cf95c0720cd8944ab771e533e/source/scale_common.cc#971)：ScaleARGBCols_C 按 16.16 源位置逐个复制四个通道，不强制源/目标相同尺寸。

这是可复用算法来源，不是已在 UniXNU/MSM8998 内核验证过的直接修复。整体 libyuv 带用户态分配和 CPU/SIMD 分派；不能直接把全库或 NEON 路径塞入当前内核，也不能让通用库直接向 device memory 发出未知宽度/对齐的写入。移植时应固定 commit、保留许可证，只选明确的 portable scalar 路径，适配内核分配和现有显存写入约束。

## 5. 建议的窄修复路径

保留 GOP 和驱动尺寸 1080×1920，保留原始请求，不向用户空间伪造 1242×2286 的物理屏幕。先仅支持已经观察到的一类请求：layer0、原点零、源目标均有界、目标覆盖真实 panel、三个未知/状态字段零、无旋转、可读取的单平面 BGRA backing。

1. **验证器与复制器联动。** 将 source extent 与 destination extent 分开；源矩形必须处于实际 IOSurface 尺寸/descriptor 范围内，目标矩形必须处于 GOP 范围内。不能继续用目标宽高替代源缓冲区的所有检查。
2. **相同尺寸保留旧快路径。** 只在存在明确 extent 差异时选择缩放，避免正常全帧退化。
3. **复用上游标量定点重采样。** 优先无插值路径建立正确完整映射，之后再评估双线性质量。分别使用真实 source stride 和 GOP rowBytes，不把源 pitch 猜成 1242×4。
4. **通过缓存行缓冲写显存。** 上游标量缩放先产生普通 RAM 中的一行，随后沿用已约束的对齐 scalar store 写入 GOP；不要让用户态库直接操作 device memory。1080 像素的单行缓冲仅需 4320 字节；双线性实现可能需要额外行缓存，仍可避免完整 8MiB staging 图像。
5. **保持完成顺序。** 全部目标像素写入并执行原有 store barrier 后，再走原始完成/退休路径；失败返回真实错误，不伪造完成。

这是基于现有观察的修复方向。开始实现前还应确认源描述符足以覆盖 1242×2286、源格式及实际 stride；原始家族已做的源矩形验证可作为支持证据，但不能替代后端对真实映射的检查。未知 dummy、其他层/变换、压缩格式继续拒绝。

## 6. 对应改动位置与验证

- `NativeLegacyDisplay::validateSwap()`：当前两组都等于 display width/height 的条件应改成所支持的有界 SRC/DST contract，而非无条件放行。
- `NativeLegacyDisplay::kickOnce()`：根据已验证的源/目标尺寸选择复制或缩放；对源 backing、目标映射重新检查；不改变资源持有和退休顺序。
- `NativeLegacyRequest.h` 或隔离的 scaler adapter：复用固定版本标量缩放逻辑，保留整数溢出、地址范围和 device-store 约束。

有意义的离线验证应包括：旧的同尺寸输出逐字节不变；1242×2286→1080×1920 的已知图案与固定上游标量结果一致；有 padding 的 source stride、极限边界和前后 guard 字节保持；驱动失败时不退休。实机需要看到缩放请求被接受、传输/完成增加，以及真实非黑内容，不能只以 reject 下降宣布成功。

## 7. 本轮结果

没有找到能直接合入的 UniXNU 同类缩放补丁。找到且读过的案例分别是 **逻辑画布居中裁剪（topaz）** 和 **任意尺寸 CPU 重采样（libyuv）**。对本机已列出的参数，后者更符合原始 SRC→DST 请求，而非裁剪或简单调整 display-scale。

本轮没有修改代码、构建新镜像或启动设备。后续实现不能承诺解决其他尚未观察到的拒绝原因；LATEST mask=600c 的矛盾继续保留，需照片核对。
