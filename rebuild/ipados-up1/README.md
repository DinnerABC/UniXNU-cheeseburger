# iPadOS UP1 构建输入快照

此目录保存历史 UP1 的四个构建后板级源码及原始构建脚本，供重建时对照。`scripts/` 中脚本是历史证据，仍含原始路径，不能从此目录直接运行；其中 `finish-build.py` 会覆盖 `out/p21/upstream-a`，禁止直接执行。

历史固件镜像为 `out/p21/upstream-a/uefi/boot-p21-up1.img`，SHA-256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`。其内核 SHA-256 为 `bb711854df65705a7e5fb28f56ea132f183e3b84ad8aa8cbd9f05937d814f662`。官方框架版本及实验来源分别见本目录 `source-inventory.json` 和 `patch-provenance.json`。

当前分支的 `package/BoardRuntime/BootLog.c` 与 UP1 的构建后快照不同；其他板级源码和上游组件还需逐项按构建记录核对。Apple 固件、内核缓存、Data seed、生成镜像和设备日志不放进 Git。现有 UP1 路径及其源码树必须保留，新的可复现构建应写入独立目录，并重新验证固件中的内核、ADT 和 payload 绑定。

本目录只是历史输入快照，尚未证明干净环境完整重建。

历史 Git 提交 `f89c28c9f83ec171ba7ee43018b0cd20766d9698` 的四个板级文件与本目录快照 SHA-256 完全一致。该提交已导出到独立的 `out/p21/up1-clean-rebuild/source/`，不包含历史构建对象。后续必须将固定上游源码和构建脚本适配到此独立目录；当前原始脚本仍硬编码旧构建树、共享 VM overlay 和输出目录。

## 2026-10-29 独立重建

本轮从上述提交导出新源码，并在 `out/p21/up1-clean-rebuild/source/` 生成独立 XNU 对象。使用固定的本机官方框架、已固定 feeds、`local.toml`、验证过的补充头文件、根卷策略材料及原 UP1 的 ADT、Data seed、trust cache；这些本地输入仍需保留。内核使用原版预链接与 panic 屏幕路径。

`clean-scripts/` 为本轮已调整到独立输出目录的预链接、载荷、UEFI overlay 和打包脚本；不要运行历史 `scripts/finish-build.py`。UEFI 构建树位于 VM `/home/dinnerabc.guest/unixnu-up1-clean/source`，新建了独立 upper 和 Build 输出，但 lower 仍引用 UP1 和更早的本地 overlay 层，因此不能称为完全自包含的源码包；这些层不要删除。EFI 模块与固件卷节哈希、编译期载荷/内核/ADT 校验值均已核对。日志、manifest 和最终镜像在 `out/p21/up1-clean-rebuild/`。新镜像仅完成一次 Fastboot 临时启动，运行结果待实机确认。
