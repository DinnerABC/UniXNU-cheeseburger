# A540 桌面常量与统一 adapter · 2026-11-07

## 接续范围

接续 `c46c3be`。本批完成桌面路径最后两个用户态缺口：真实 Metal function constants 的快照／声明解析／specialization，以及统一 21H461 adapter 对桌面 backend 的接入。未启动设备、未改安全策略。

## 实现

- `NativeMetal540ConstantReflection.{h,inc}`：上游 `inspectAIRConstants` 封装为有界 C bridge（编译进同一 compiler dylib），只跨复制的声明元数据，不导出 LLVM/NIR/C++ 对象。
- `NativeMetal540DesktopConstants.{h,mm}`：严格按目标 encoding（`@16@0:8`/`Q16@0:8`/`^v16@0:8`）验证 `MTLFunctionConstantValuesInternal`/`MTLIndexedConstantValue`/`MTLNamedConstantValue` 后复制快照；索引与命名值都来自 runtime 自身对象，布尔形状规范化；调用者 `reset` 不影响副本。
- `NativeMetal540DesktopLibrary`：`load` 接受 inspector；`function(name,values,inspector)` 先经原 AIR 声明检查（重复 index/name、非法 type/required 拒绝），再 `resolveFunctionConstants`（缺 required 报错、type mismatch 拒绝），最终按 index/type/值 生成 specialization 缓存键——不同常量组合不会共用工件；nil 常量编译进同一工件。
- `NativeMetal540DesktopObjects`：`newFunctionWithName:constantValues:`/异步变体；资源 `device` 属性返回独立 public identity（`setPublicDeviceIdentity:`/`publicDeviceIdentity`），内部执行仍用同一 backend——跨对象校验比较 public identity，系统包一层时不会拿到 backend 本体。
- 统一 `NativeMetal540DeviceAdapter`：新增 `NativeMetal540CreateInitializedDesktopDevice(backend,service,error)`，同一 qualified 21H461 类；desktop backend 要求实现 `newCommandQueueWithMaxCommandBufferCount:`。新增转发 `newCommandQueueWithMaxCommandBufferCount:`（`@24@0:8Q16`）、`supportsTextureSampleCount:`（`B24@0:8Q16`）、`newSamplerStateWithDescriptor:`（`@24@0:8@16`，desktop 才转发到 backend，旧固定 backend 维持 nil）。desktop `initLimits` 反映真实桌面上限（8 附件/16 属性/8 绑定/16384²/16MiB buffer/64B linear 对齐/1 inflight）；旧 backend limits 不变。旧 `NativeMetal540CreateInitializedDevice` 行为不变。

## 来源核对

固定 sagit `673f850…`：`snapshotConstants`/`constantReflection`（QcomMTL.mm:890–949）的 copy-then-read 与 encoding 校验语义；`FunctionConstants.h::resolveFunctionConstants` 原样复用；`inspectAIRConstants` 只读声明提取。命名→index 不猜，必须来自原 AIR。三个新 adapter 方法的 encoding 与本机 protocol audit 一致。

## 验证

- 104 项全回归 PASS（含旧 device-api 3、graphics-objects 3、桌面 4）。
- 桌面 ObjC ASan/UBSan：float3/bool 索引常量快照逐字节比对、`reset` 后副本不变、命名常量快照、原 AIR 声明经 inspector 解析、specialization 缓存键、不匹配 VS/FS 拒绝、standard 顶点 layout。
- `scripts/p21-build-metal-runtime-compiler.py --host-test`：同一源码构建 host 后端（`774b5f23…`）供实际 AIR 合同测试；target 后端（`48e1d5f4…`，签名后 `fa7eda41…`）链实际 iOS LLVM，linkage 无 host LLVM。host-test 不接触设备，不声明 runtime 语义已验证。
- `p21-build-metal-desktop-objects.py`：新 iOS 产品 `NativeMetal540Desktop.dylib` `7dbbe704…` + compiler `fa7eda41…`，codesign 严格通过。
- 本轮修正过程：孤儿 `declarations` 定义、快照类型不匹配、命名常量 `reset` 断言、测试链接旧编译器缺符号——失败日志保留在 desktop-tests 目录。

## 证据

NAS `out/p21/p21-metal-desktop-constants-20261107T053258Z`（sourcepins/双编译器 build.json/产品/日志/manifest）；local `native540/constants-adapter-20261107`。

## 仍未完成（本轮不启动设备）

1. 系统 registration 语义闭包：`argumentBuffersSupport` 依赖（indirectArgumentBufferCapabilities）已核实非 abstract，但 195 BOOL 之外的 `readWriteTextureSupport`/`maxArgumentBufferSamplerCount` 及继承 abstract 全清单证据不足；principal 仍 nil，不 fake protocol。
2. 设备端 runtime LLVM C 语义/加载/编译未实机验证；export 通过不等于语义正确。
3. Drawable/display-reader 呈现与 reader-done 复用未接；GPU copy CPU 帧不算合成。
