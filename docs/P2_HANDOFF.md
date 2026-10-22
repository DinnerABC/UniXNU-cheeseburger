# P2 续接检查点

## 本轮停止位置

原版 UEFI、原生 boot image、集成版 UEFI/boot image 均已真实编译通过并导出。FV 内 UniXNU + 源编译 UniXNUDxe + DarwinBoot 的内容哈希验证通过。没有待继续等待的编译进程；VM 将正常停止保留磁盘。不要从 P1 重做。

先读 `P2_BUILD_REPORT.md`、`P2_UNIXNU_INTEGRATION.md`，再看 `out/p2/final-checks.json`。所有 P1 证据保留。新 CI 为 `.github/workflows/p2.yml`，未远程运行。

## 持久文件

- 设备根目录：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/`。
- 主机源管理目录：`../upstream/`，版本见 sources.lock.json 和 out/p2/submodules.lock.txt。
- 所有成功导出：`out/p2/baseline/`、`out/p2/integrated/`、`out/p2/unixnu-package/`、`out/p2/repacked/`。
- 所有尝试/退出码：`out/p2/logs/`，不要删除失败记录。
- VM：`.l/p2/disk`，Lima 二进制：`.work/p2-tools/`，镜像缓存：`.work/downloads/`。
- guest 原版：`/home/dinnerabc.guest/unixnu-p2/original`；集成版：同级 `integrated`。
- source 副本在项目内 VM 磁盘的 ext4 中；不要在 macOS 大小写不敏感共享树直接编译 SimpleInit。

## 恢复 VM（在设备根目录）

```sh
bash scripts/p2-vm.sh start --tty=false p2
bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 uname -a
```

需要真实重编译已有集成副本时：

```sh
python3 scripts/p2run.py uefi-integrated-resume -- bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 bash scripts/p2-linux-build.sh /home/dinnerabc.guest/unixnu-p2/integrated /project/UniXNU-cheeseburger/out/p2/integrated
```

不要覆盖已有成功日志；给新尝试使用新的 label。已有副本已应用适配，不要对它再次运行 p2-integrate.py。

从既有主机源重新创建全新 ext4 副本时，在 guest 用 rsync -a 排除根 workspace，并排除 SimpleInit 的 build 目录；保留 .git 和全部子模块。已有 Ubuntu 软件不必重装。若 VM 丢失，config/p2/lima.yaml 和 out/p2/vm-inputs.json 记录镜像与摘要，scripts/p2-linux-setup.sh 是已验证安装入口。不要重下载有校验通过缓存的大文件。

所有下载默认直连。p2-vm.sh 清除代理环境变量，Lima 配置 propagateProxyEnv=false 且 proxy env 为空，APT 强制 DIRECT。不要擅自切到计费代理。

## 下一步最小实验

先完善只读的运行时 UEFI 协议/异常级/内存图探针及可观测日志方案。当前没有硬件启动证据；如继续 XNU，先解决 Xcode/SDK、独立 IPSW/prelink 与真实 MSM8998 ADT 参数。不要为获得绿灯删除 example=true，不要生成或执行刷写命令。
