# iPadOS UP1 构建输入快照

此目录保存历史 UP1 的四个构建后板级源码及原始构建脚本，供重建时对照。`scripts/` 中脚本是历史证据，仍含原始路径，不能从此目录直接运行；其中 `finish-build.py` 会覆盖 `out/p21/upstream-a`，禁止直接执行。

历史固件镜像为 `out/p21/upstream-a/uefi/boot-p21-up1.img`，SHA-256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`。其内核 SHA-256 为 `bb711854df65705a7e5fb28f56ea132f183e3b84ad8aa8cbd9f05937d814f662`。官方框架版本及实验来源分别见本目录 `source-inventory.json` 和 `patch-provenance.json`。

当前分支的 `package/BoardRuntime/BootLog.c` 与 UP1 的构建后快照不同；其他板级源码和上游组件还需逐项按构建记录核对。Apple 固件、内核缓存、Data seed、生成镜像和设备日志不放进 Git。现有 UP1 路径及其源码树必须保留，新的可复现构建应写入独立目录，并重新验证固件中的内核、ADT 和 payload 绑定。

本目录只是历史输入快照，尚未证明干净环境完整重建。
