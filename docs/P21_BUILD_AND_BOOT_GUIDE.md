# P21 iPadOS 构建与临时启动指南

本指南记录本机已实际使用并验证过的 P21（iPadOS 17.7.11 / 21H461）构建链。项目根目录 `/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`，分支 `ipados-17.7.11`。开始前先读根目录 `AGENTS.md`、`HANDOFF.md` 和 `docs/P21_OFFICIAL_WORKFLOW_AUDIT.md`；保留其他 agent 的改动与历史镜像。

## 0. 已知可启动基线

- 用户确认进入系统：`out/p21/phone-ui-d2/uefi/boot-p21-up1.img`，SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`；画面仍为平板布局、触摸不可用。
- 对应日志：`out/p21/persistent-logs/20261029T172431Z/`，boot ID `0x5a0ea281`，`frames=14`、`SWAP=14`、`online=8`、`TOUCH error=-54`。
- 官方框架固定源码 `.work/p21-official-framework`；板级运行时源码 `package/BoardRuntime/`。
- 固件在 Linux VM 构建；基线 overlay `/home/dinnerabc.guest/unixnu-up1-phone-d2/source`，其 lower 依赖历史层（见该候选的 `scripts/guest-overlay.sh`）。**不要删除旧层，也不要原地改写旧候选的 pin。**

## 1. 创建独立候选

1. 从基线复制候选目录（排除 `private`），例如：
   ```sh
   rsync -a --exclude 'private' out/p21/phone-ui-d2/ out/p21/<name>/
   ```
   目录中 `source-tree/` 是完整项目副本（含 `board.toml`、`configs`、`package/BoardRuntime`、已解析的 `out/p21/official-adaptation/resolved/...` 与 `build/xnu/ios-obj` 缓存）。
2. 把本轮的板级/包源码改动同步进 `out/p21/<name>/source-tree/package/BoardRuntime/`。
3. 把候选 `scripts/*.py`、`*.sh` 中硬编码的旧候选名改成新名（`build-payload.py`、`prelink.py`、`prepare-uefi.py`、`pack.py`、`prepare-source.py`）。

## 2. 编译内核（官方框架 API）

驱动脚本参照 `scripts/p21-compile-r18.py` / `scripts/p21-compile-touch-drive.py`：

- `R = out/p21/<name>/source-tree`；`U = <项目>/.work/p21-official-framework`（绝对路径）。
- 流程：`parser().parse_args(['tree','-D',str(R),'--out',str(R/'out/p21/official-adaptation'/'resolved'),'-j','6'])` → `Context` → `feeds.resolve` → `patches.prepare` → 执行项目 `scripts/p21-restore-official-headers.py` → 复制项目 `scripts/p21-build-board-runtime.py` 到 `c.tree/scripts` → `kernel.build(c,m)`。
- **必须保留代理**：`prepare-xnu-source.py` 会从 GitHub 取 DriverKit 头，本机只能经 `http://127.0.0.1:7897`；驱动里不要清空 `HTTP(S)_PROXY`。
- 编译产物在 `<tree>/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/`；记录 `kernel-source-build.json`。

## 3. 预链接 + panic-screen

1. **原生内核取 `mach.development.bcm2837.unstripped.noctf`**（不是 stripped 的 `mach.development.bcm2837`），复制为 `out/p21/<name>/native/kernel.macho`。
2. 运行候选 `scripts/prelink.py`（默认读 `native/kernel.macho`，断言 donor kernelcache 哈希）；输出 `prelink-final/kernel/kernel.macho`。
3. 复制到 `out/p21/<name>/kernel/kernel.macho`，打 panic-screen：
   ```sh
   UNIXNU_UEFI=/Volumes/NAS-MacData/Project/IOS/upstream/uefi-sheng \
     python3 .work/p21-official-framework/scripts/patch-xnu-entry-probe.py \
     out/p21/<name>/kernel/kernel.macho --panic-screen --no-reset
   ```
4. 写 `kernel/validation.json`（result PASS、sha256、bytes）。

## 4. payload

运行候选 `scripts/build-payload.py`：生成 `payload/{payload.bundle,payload.container,payload.lzma,manifest.json}` 与 `config/{CompressedPayload.h,Expected.h}`，记录 manifest 哈希与长度。

## 5. 固件（独立 overlay）

1. 以基线 `scripts/guest-overlay.sh` 为模板创建新 overlay（换 `SOURCE`/`BASE`/marker，`LOWER` 前置基线的 `upper`），例如 `/home/dinnerabc.guest/unixnu-up1-<name>`；`workspace/Build` 绑定到新 `BASE/build`（空目录即全量编译）。
2. 在新 overlay 的 VM 中执行：`bash out/p21/<name>/scripts/guest-overlay.sh`，再 `python3 out/p21/<name>/scripts/prepare-uefi.py`（把两个生成头文件写进该 overlay）。
3. 编译并打包固件：
   ```sh
   bash scripts/p2-linux-build.sh /home/dinnerabc.guest/unixnu-up1-<name>/source \
     /project/UniXNU-cheeseburger/out/p21/<name>/uefi
   ```
   宿主执行时记得 `LIMA_HOME=$PWD/.l`，否则 `limactl` 找不到 `p2`。
4. 从 `workspace/Build/cheeseburger/RELEASE_GCC5/FV/MSM8998PKG_UEFI.fd` 与设备 DTB 导出到 `out/p21/<name>/uefi/`；扫描 AARCH64 EFI 模块核对 payload/kernel/ADT 三项哈希（pins 文件放在项目内，VM 看不到宿主的 `/tmp`）。

## 6. 打包 / 门禁 / 启动

1. 运行候选 `scripts/pack.py` 生成 `uefi/boot-p21-up1.img`，确认镜像内固件、DTB、payload 可回读，写 `uefi/packaging.json`。
2. `python3 scripts/unattended/preboot-gate-p21.py out/p21/<name>` 必须 PASS。
3. 启动前核对：镜像哈希、最近 boot ID、设备 `3b25cc7c`、只保留一个 ADB 服务、无旧监视器/等待中的 Fastboot。设备在 Recovery 且用户当前授权时：
   ```sh
   .work/p3-tools-35/platform-tools/fastboot -s 3b25cc7c boot <本轮镜像路径>
   ```
4. `Sending/Booting OKAY` 只代表传输被接受。记录屏幕结果；设备回到 Recovery 后用系统实际 ADB 端口回收日志（`scripts/logging/read-cheeseburger-log.py`，或 `scripts/unattended/monitor.py` 轮询），比较 boot ID 与窗口哈希，重点看 `TOUCH`（reset/I2C/F01/F12/HID）、`DISPLAY/SWAP/SCALE`、`frames`。
5. 更新 `HANDOFF.md`：实际镜像、源码提交、构建与设备结果、风险与下一步；按约定提交源码与文档，**不提交镜像、签名材料和设备私有日志**。

## 7. 常见坑

- 误用 stripped 内核：预链接会通过，但产物与基线不一致；必须用 `.unstripped.noctf`。
- 漏掉 panic-screen 步骤：内核哈希与基线流程不一致。
- 清空代理：`prepare-xnu-source.py` 会以 `SSL: UNEXPECTED_EOF_WHILE_READING` 失败。
- 复用旧 overlay 或旧回执：pin 与镜像可能不匹配；每个候选使用独立 overlay 与自己的回执。
- 设备异常重启会落回 Recovery（BCB 可能是 `boot-recovery`）；没有新日志时不能据此判断“没启动”。
- 观测窗口：显示/scale 在 180–240 s 内出现；触摸探针更早；建议跑满 ≥300 s 再手动复位。

## 8. 已执行实例（touch-drive）

- 内核编译 `db6e18f4…`（156 s）→ unstripped 预链接 `d440a90c…` → panic-screen 后 `9efd466e…`；
- payload `571e414a…`；独立 overlay 全量固件 FD `9d2fce81…`，三项 pins 通过；
- 镜像 `out/p21/touch-drive/uefi/boot-p21-up1.img` `55c694fb…`（82,595,840 bytes），门禁 PASS（644 字节参数）。