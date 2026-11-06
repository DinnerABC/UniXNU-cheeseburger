# Native540 离线工具与固定测试输入

本目录适配固定 sagit `673f85070fe9f7872793015eb230a5c3987d4a8c` 与 Mesa 24.3.4 的 A540 编译后端；设备用户态仍使用本板 `NativeAdrenoABI.h`。不得把 A740 命令或其他版本 Metal ABI 直接复制到本板。

- 顶层 `.cpp`／`.py` 为编译、准入和导出工具；`scripts/p21-build-native540.py` 在本地隔离目录构建 macOS 离线后端。设备不运行 LLVM。
- 顶层 `*_fixture.h` 与 `native540_objects_registry.h` 是有界测试所需的固定派生 ISA、反射和输入身份，不是通用 shader 验证器。AIR fixture 的唯一编译依赖 `evidence/air-simple-20261106T050950Z/padding-gradient-copy/NativeMetal540AIRFixture.h` 显式纳入 Git；保留该头的原始字节和哈希，不改机器码或伪造反射。
- `evidence/` 的其余内容仅保存在本机，包括后端构建回执、编译日志、原始／中间产物及本机路径。Git 忽略整个目录，仅对上述头文件显式添加；不提交 Apple metallib、AIR／LLVM、签名材料、设备日志或可执行程序。
- 使用原始库的测试需要用户已有的、哈希限定的私有 OS 输入和本地编译缓存。缺少它们不能宣称全部测试通过；脚本拒绝覆盖已有 evidence，运行时指定新的输出目录。保留的历史 manifest 路径不是可独立分发的 Apple 输入。

宿主模拟、类型／哈希检查和编译成功不等于设备 shader 执行。本机实际像素／fence／退休证明必须引用同一镜像与新 boot ID 的设备日志。当前固定 MLCompute／SpriteKit workload 和 64×64 RGBA8 路径不等于完整 Metal、系统默认设备、presentation 或 UI GPU 合成。
