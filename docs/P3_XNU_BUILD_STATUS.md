# P3 XNU 编译状态

尚未完成 XNU 内核编译，不能报告成功。out/p3/xnu-status.json 为本轮真实工具链检查：xcodebuild -version 退出1，iphoneos SDK 查询退出1，CLT Clang 可用。当前无完整 Xcode/iphoneos SDK，不能用 CLT 代替。

board.toml 保留 example=true，cheeseburger_defconfig 仍是研究骨架；缺合格平台 archives、prelink/IPSW 选择与 Apple ADT。P3 已取得真实设备 FDT、RAM/reserved 节点和 iomem，解决了部分采集权限问题，但不是 XNU 可直接接受的 ADT。

在建立真实启动链观测后，最小内核目标是通用 ARM64 XNU 编译及可识别早期控制台，而不是 iOS 桌面。后续必须逐一核对 4KiB 页补丁、CPU/MPIDR、GIC、计时器、早期内存映射、异常级和启动参数；不复制 sheng 平台地址、身份和 Trust Cache 哈希。

本轮不使用 GitHub Actions，不伪造 SDK/内核产物。P2 原有产物不覆盖。

UEFI 方面的前置条件已有真实进展：UniXNU图形菜单、EL1、DarwinBoot Loader协议、GOP/SimpleFS/BlockIO均已观察。DarwinBoot尚未调用内核入口，不能把协议安装当作XNU启动。

还必须注意：固定上游 scripts/build-xnu-4k-baseline.sh 默认固定 XNU xnu-10063.141.1 / d8b80295118ef25ac3a784134bcf95cd8e88109f，并在 installhdrs/最终编译使用 MACHINE_CONFIGS=BCM2837，最终默认 SDKROOT=iphoneos。不能直接把该默认配置标为MSM8998平台适配；XNU_SDKROOT虽允许覆盖，也不能自动解决libfirehose的xcodebuild依赖和板级适配。本轮没有在该错误的平台标签下制造“MSM8998内核成功”。

下一步：在项目内建立独立XNU工作副本和cheeseburger配置，限定通用4KiB补丁，先资格验证通用ARM64基线，再接入真实CPU/GIC/内存与ADT。DarwinBoot接口还需核对packed ramdisk中的ADT/Trust Cache政策，不能把Linux FDT直接作为DeviceTree请求参数传入（当前实现对非空该字段返回Unsupported）。
