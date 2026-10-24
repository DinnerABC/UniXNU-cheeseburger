# P6 本地执行记录

目标已按用户要求改为 **iPhone iOS**。P6-v1 已真实增量编译、打包，并对真实 OnePlus 5 执行了一次 `fastboot boot`。观察 240 秒后未恢复通信，控制器以 `NO_TRANSPORT_MANUAL_RECOVERY` 停止（退出码 2）。用户随后提供了本轮屏幕照片，已保存为 `evidence/p6/boot-v1-s05.jpg`。照片确认 S00 品红、S01 黄、S03 青、S05 白，证明实际执行到 MMU 开启前检查点。

P6 最新实机证据已超过 P5 的 `__start`：确认 BootArgs 有限检查和页表构造循环完成，最后可确认位置为 **S05，写 SCTLR 之前**。**未证实 MMU 启用、首段 C 初始化、CPU/VM/Platform Expert 或 iOS 启动。**

## 实际产物

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `out/p6/kernel/kernel.macho` | 19277552 | `b6c5ce0062a6318d1926ac06cd017d0f2afaf757d125f22d645b55b46ee89baf` |
| `out/p6/adt/kernel.macho.adt` | 13616 | `cec5013d014ea9a5c659a388691ab6446e889a43a4e0111e24a5f0e0075f0369` |
| `out/p6/uefi/boot-p6-xnu.img` | 25427968 | `af72619bc49b1dd7b7942259779ae225639a8dd1facd0fe91b7d3369e941c0c3` |

其他固件、payload、源码和补丁哈希见 `out/p6/artifacts.json`。P5 原始文件及只读副本的复核见 `out/p6/baseline-check.json`，全部匹配；P1–P5 原始成果未删除。源码提交见 `out/p6/sources.json`，沿用 P4 固定工具链，没有升级依赖或复制工作树。

## 编译与验证

- XNU 仅修改 `start.s`，停止 Linux VM 后以原 `-j2` 流程增量编译、重链接、执行既有 crypto HMAC sealing；最后一次 `link-kernel-03` 退出 0，约 23 秒。沿用 ArmCpu、ArmDTPlatform 与真实 POST bootstrap 库，未重新进行大规模 XNU 构建。
- P6 UEFI 使用 P5 之上的独立 overlay 和独立 Build；P3/P4/P5 下层源码不变。`build-uefi-01` 真实编译 151 秒、退出 0；随后导出、AArch64 PE/FV 对照、原生 boot image 回读和 P6 RAM payload 打包均退出 0。
- 独立 ADT 沿用已验证的 P5 内容；`check-adt-01` 使用实际 DarwinBoot `AdtNode` 函数重新编译并解析，通过。没有添加虚构硬件参数、Apple 身份或 Trust Cache。
- `probe-emulation-06` 执行最终链接的 ARM64 指令：三个色条、寄存器保留范围、异常记录、有效/零 framebuffer，以及注入 SCTLR.M 输入的禁止物理 framebuffer 分支均通过。
- `bootstrap-emulation-03` 执行最终链接的页表构造指令，检查入口、C 入口、早期异常向量及记录区的 V=P/KVA 映射；包含显式错误大页偏移负例，通过。此测试没有开启模拟器 MMU，更不是实机 MMU 证明。
- `execution-map-01` 用实际目标的编译参数提取页粒度、TCR/SCTLR 和页表池常量，并与最终 Mach-O 初始化数据核对，退出 0。

所有命令、完整 stdout/stderr、退出码及耗时保存在 `out/p6/logs/` 的同名 `.log` / `.json`。早期失败没有删除：汇编宏语法失败、模拟器 JIT/夹具问题、零 framebuffer 路径缺陷均有记录。零 framebuffer 缺陷已修复并重新链接、重新测试；尝试完整模拟器 MMU 的 `probe-emulation-05` 失败，不能用之后的分支单测替代真实 MMU 验证。

## 本轮设备测试

run_id：`20261022T020508Z-20037c90`。

记录目录：`out/p6/device-loop/20261022T020508Z-20037c90/`。启动前重新核验设备身份、目标 DTB、镜像 SHA256、内核/ADT/config 和原有备份哈希，随后只执行临时启动。序列号及设备原始输出保留在 private 子目录，不写入公开报告。

Fastboot 接受镜像，之后 USB/ADB/Fastboot 均失联。240 秒观察结束后停止，没有重复发送同一镜像，没有尝试未经验证的重置、EDL 或分区写入。结果 `summary.json` 与 `out/p6/stages.json` 严格区分传输状态和执行证据。

本轮照片已经归档；设备仍需人工长按电源恢复 Android，尚未收到恢复确认。不能把最后可见 S05 直接认作故障指令位置。尚未取得本轮异常数值，也未实现 EBS 后日志自动回收。
