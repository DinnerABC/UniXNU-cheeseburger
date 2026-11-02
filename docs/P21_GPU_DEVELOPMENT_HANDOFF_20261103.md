# P21 GPU 开发交接（2026-11-03）

## 当前目标和边界

OnePlus 5 / MSM8998（cheeseburger，设备 `3b25cc7c`）运行 iPadOS 17.7.11 / 21H461。当前目标是推进 Adreno 540 用户态提交、GPU 实际渲染和 Metal 对接；当前仍不具备完整 GPU 显示。项目能够 CPU 软件显示桌面。`frames` 只代表显示传输，不能当作 GPU 渲染。内核 probe 的 fence 成功也不能代替用户态 BO、提交、fence 和回读成功。

遵循根目录 `AGENTS.md` 的上游优先和本地编译规则；每一轮应批量实现互相关联的功能并增加必要的有界诊断，不要只做日志、不停做对照而不开发。先阅读 `HANDOFF.md` 顶部最新记录、本文件、`docs/P21_GPU_HANDOFF_20261102.md`、`.work/p21-official-framework/docs/porting/gpu.md`。官方 sheng 在 `.work/p21-official-framework/drivers/Adreno740/`；本地同 SoC sagit 在 `docs/upstream/reference/sagit/`。参考 sheng 的接口、内存生命周期和同步；具体 GPU 寄存器、固件与命令序列以 A540/同 SoC 实现为准，不可直接套用 A740 硬件参数。

NAS 正式仓库：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`。保留现存其他 agent 的未跟踪文件 `package/BoardRuntime/CheeseburgerLaunchdCache.c`、`patches/xnu/0010-cheeseburger-launchd-cache-overlay.patch`、`tests/launchd_cache_model.cpp`，不得随意覆盖或清理。提交每次源码和文档修改；私有镜像、日志和固件不要提交。

## 本轮已做

代码提交 `b9f698f`。`CheeseburgerGpuService.cpp` 参考 sheng 的 BO 生命周期：分配后发布缓存并设置物理页 `VM_WIMG_WCOMB`，完成安全 GPU 解映射后恢复默认属性，提交前加屏障；`CheeseburgerGpuCp.cpp` 把 GPU 域、micro-MMU 前置失败改为停止 doorbell，并将一次提交限制移入锁。仓库 `AGENTS.md` 已新增 GPU 开发优先条款。这是候选实现，缓存属性、并发和实际 GPU 访问都必须由新设备日志确认。

上一轮日志 `out/p21/persistent-logs/20261102T165108Z/`、boot `0x4633ae0d`，68 rounds、0 写错。GPU 核心域读取 `0x140000/0x140000`、micro `0x178/0`；用户提交 `GSUB rc=-4 stage=3 fenced=0 pf=0/1`。`pf=0/1` 表示已配置的预置标记未被 CPU 观测到，不能证明 CPU/GPU 可见性或断言 CP 一定没有执行。CPU 显示 6 帧；本轮没有 GPU 用户 fence 成功证据。

## 本地编译和 NAS 回写

权威源码在 NAS，本机编译树 `/Users/dinnerabc/unixnu-p21/atos-gate/source-tree`；脚本会取得构建锁、增量同步板级源码、确认 `tree/kits/cheeseburger` 指向本机 package、使用本机官方头文件缓存和补丁，记录 HEAD、哈希，并把编译结果回写 NAS。用户要求确需下载官方头文件时使用 `127.0.0.1:7897`，下载后复用缓存。

本轮候选 `out/p21/gpu-dev-batch/`，其 scripts、ADT、配置和 trust 以 `gpu-smmu-tlb` 复制并重定向，Data seed 软链接到该候选的已验证 `1e8b97fb…`，不要改 seed identity 或自动 reseed。新候选应隔离，不能并发使用同一对象树。示例：

```sh
cd /Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger
P21_CANDIDATE="$PWD/out/p21/gpu-dev-batch" /opt/homebrew/bin/python3.13 scripts/p21-compile-atos-gate.py
```

使用本轮 local XNU `build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf` 作为候选 `native/kernel.macho` 输入候选 `scripts/prelink.py`；复制 prelink 结果到 `kernel/kernel.macho` 后，再运行 `.work/p21-official-framework/scripts/patch-xnu-entry-probe.py` 修改候选 kernel（不能改 prelink 原件，否则校验会失败）；运行 `scripts/check-kernel.py`、`scripts/build-payload.py`。核对记录和实际文件哈希一致。

p2 Lima VM 是 Linux ARM64 UEFI 固件编译环境，保存了此前适配的分层 overlay，不可当空环境随意清空。本轮原 24 GiB 虚拟盘曾满，已扩容为 40 GiB，并将 `/dev/vda1` ext4 在线扩容；其余 overlay 保留。构建源使用独立 `/home/dinnerabc.guest/unixnu-up1-gpu-dev-batch/source`，构建脚本 `out/p21/gpu-dev-batch/scripts/guest-overlay.sh`、`prepare-uefi.py` 与 `scan-pins.py`；从项目内 `bash scripts/p2-vm.sh shell p2 -- ...` 执行。先挂载本轮 overlay，再运行 prepare 更新编译期 `CompressedPayload.h`、`Expected.h`，再从 VM 运行 `scripts/p2-linux-build.sh`。只清理本轮因磁盘满而不完整的 build 对象，不要删除其他候选或上游输入。

固件构建真实成功后保存构建记录，使用候选 `scan-pins.py` 核实固件内部的本轮 kernel/ADT/payload 哈希，再用 `scripts/pack.py` 和 `scripts/unattended/preboot-gate-p21.py out/p21/gpu-dev-batch` 组装、校验。固件、payload、ADT、内核、镜像必须来自同一轮，`fastboot OKAY` 只代表传输。

## 设备启动和日志

用户要求本轮只进行一次 `fastboot -s 3b25cc7c boot <本轮镜像>`，默认不写 boot/recovery/GPT。此前只授权 misc 的 Recovery command 字段，必须保留其余内容、写回读校验；只保留系统已有的一个 ADB 服务。上一轮已经先回收；本轮启动后按用户最新要求**不要开启后台监视**，由接手 agent 在设备回 Recovery 后读取。若新状态不明，先检查设备模式，不要自动循环重启。

持久日志在 userdata 相对 10 GiB、32 MiB 的专用窗口；确认设备为 root Recovery，用现有 ADB 端口：

```sh
.work/p3-tools-35/platform-tools/adb devices -l
CHEESEBURGER_ADB_SERVER=tcp:localhost:5037 /opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py
```

先确认系统实际端口，再设置变量，不要新启动第二个 ADB 服务。结果在 `out/p21/persistent-logs/<UTC>/`，阅读 `retrieval.json`、`DIAGNOSTIC_OVERVIEW.md` 和 `decoded/`，保留原始窗口；核对新 boot ID 和窗口哈希，不能把上轮 `0x4633ae0d` 的旧记录当本轮。极早期 panic 或写入未启动可能没有新持久日志。

重点看：首个 panic/启动阶段；内核 fence 和服务发布；`GPWR` 前置电源/micro-MMU 状态；`GPSVC` 的 open、BO、map、submit、fence；`GSUB` 的返回、阶段、`pf` 和提交序号；CPU 回读实际值；显示 frames/SWAP、触摸、Data state、自动返回路径。仅当状态门禁触发时，不得推断已提交；`ffffffff` 若为失败后未读占位也不能当真实寄存器值。不要因缺少 CPU fence 就释放仍可能由 GPU 访问的页。

## 后续开发

1. 基于本轮真实结果继续官方路线：用户提交同 boot 完成 `IOServiceOpen → BO/map → IB submit → fence → CPU 多值回读`。若成功，批量补齐资源/IOSurface 与最小像素验证，随后适配 Metal 与显示交接；各阶段分别报告，不要把 discovery、软件帧或内核 probe fence 当 GPU 合成成功。
2. 若仍失败，先对照 sheng `NativeAdrenoMemory/NativeAdrenoVm/NativeAdrenoSubmit` 的 BO cache mode、提交与 TLB 顺序，以及 sagit A540 的 ring/IB/fence 语义，优先直接修复确证差异，并在同一版保留必要有界诊断；不要改掉已验证的 Data、UEFI、屏幕和触摸，也不要循环启动。
3. 每次构建前记录工作树和输入，每次开发完成提交；更新 `HANDOFF.md` 顶部，用路径、boot ID、哈希、设备状态、已证实结果、未验证事项和可以直接执行的下一步交接。主机测试和编译只能证明相应阶段，不冒充实机结果。

本轮启动结果、最终镜像哈希与新日志状态以 `HANDOFF.md` 顶部的后续补充为准。

## 本轮最终启动记录

镜像 `out/p21/gpu-dev-batch/uefi/boot-p21-up1.img` SHA-256 `2cf41785989e2e23202d0075de9a056ce98adbd121639bd3146ece875a016070`，内核 `d1dbe111bffc3ad71d7bd99cbc2507f48e4f14047561465e2a00948273aeb319`，固件 `2fedc7553d713097a7c64d717be2fec9a1c4d6d3e33466db9d137a57a15b61e6`，payload `18a3f6c660ce31ba2a60d44cd25ab95ca0ef0be2a5af10043eada7027958e9f5`；gate `gate-20261102T171025Z.json` PASS，`boot-once.log` 记录本轮唯一一次临时启动 Sending/Booting OKAY。misc command 已回读一致。根据用户最新要求，启动后不监视、不回收；设备是否进入系统、GPU 提交及自动返回均待下一位 agent 根据本轮新日志确认。
