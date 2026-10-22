# P4 XNU 构建实证

## 已通过的基础内核

XNU 10063.141.1（Darwin 23.6.0），提交 `d8b80295118ef25ac3a784134bcf95cd8e88109f`，来自 Apple OSS。依赖版本见 `out/p4/sources.json`；设备独立目标 `DEVELOPMENT_ARM64_MSM8998`。没有复制 sheng 的预编译内核、物理地址或 IPSW 身份。

- 文件：`out/cheeseburger/kernel/kernel.macho`；归档副本：`out/p4/kernel-baseline/kernel.macho`。
- 大小：18895248 字节。
- SHA256：`5f39023168437d4cca9fde54ab07cbda2a50577b4479a04169fc24c4c0d1ae49`。
- ARM64、MH_EXECUTE，LC_UNIXTHREAD 与真实 `__start` 符号一致：`0xfffffff0073e8488`。
- `_arm_init`、`_kernel_bootstrap`、`_pmap_bootstrap`、`_BootArgs` 存在，未解析符号 0。
- `out/cheeseburger/kernel/validation.json` 和 `out/p4/kernel-validation/` 保存完整工具检查。

`build-xnu-06.log` 的真实 make 已成功，但随后的导出检查误找 `_start`（正确名称为 `__start`），故该次整体退出码仍为 1。修复检查后 `validate-kernel-01.json` 退出 0。不能将这个历史退出 1 改写为 0。

## 工具链与复现

Xcode 27.1 / 27A9275，iPhoneOS SDK 27.1，macOS SDK 27.0；用户已同意许可并完成首次安装。`iig`、`migcom` 可用，当前不需安装模拟器。项目进程通过 `scripts/p4-env.sh` 指定 DEVELOPER_DIR，不修改全局 xcode-select。

唯一工作树：`.work/p4-unixnu/.cache/xnu-work`。先执行 `scripts/p4-fetch.py` 获取固定依赖，再按 P4 日志中的头安装、AvailabilityVersions、firehose 命令配置已有工具链；`scripts/p4-prepare-xnu.py` 应用选定通用补丁并生成独立目标。所有准备命令及失败尝试保存在 `out/p4/logs/`。

```bash
source scripts/p4-env.sh
python3 scripts/p4-prepare-xnu.py
python3 scripts/p4run.py build-xnu-NEXT -- bash scripts/p4-build-and-export.sh
```

XNU 构建会查询主机内存；受限沙箱禁止 sysctl 时必须通过工具批准的本机执行环境运行，不能把环境查询失败当源码失败。16 GiB 主机限制 `MAKEJOBS=-j2`，大规模内核编译时关闭 P2 VM。

`patches/p4/xnu-build-and-msm8998.patch` 包含完整设备目标和通用兼容性变更，包括公开 Entitlements 头路径、4 KiB 修改和真实启动汇编/PMAP 源选择。源码选择宏不是对 Apple AMCC 硬件存在的声明；AMCC 另设开关并保持关闭。PMU 计数器 0 为禁用策略，不是实测硬件数量。

## 已编译的运行组件与候选链接

- corecrypto：529 个 C 源文件，普通和命名空间版本均通过；记录 `build-corecrypto-plain-02`、`build-corecrypto-namespace-01`。固定第三方快照 `11fba65b12d8830630e248dfa8344caf8b065f26`，未对 Apple 原始归档认证，不能称为认证加密实现。
- ArmCpu / ArmDTPlatform：`build-arm-drivers-01` 退出 0，实际 ARM64 库位于 `out/p4/arm-drivers/`；使用成功 MSM8998 内核的 IOCPU 编译记录复用 ABI。ArmCpu 定时器已改为读取实机 ADT 的 PPI19。存在上游风格/原型警告，完整日志保留，未声称无警告。
- crypto 启动回调：`build-crypto-bootstrap-01` 退出 0，从锁定上游 source-corecrypto14-bootstrap 补丁构建真正 STARTUP 回调，保留完整 POST 与失败 panic；未跳过自检。
- 独立候选：`scripts/p4-link-platform-kernel.sh` 将上述库链接到 `out/p4/kernel-platform/`，与基础内核分开。POST HMAC 按上游正常构建约定填充，不等同于自检实际执行。
- 首次候选链接 `link-platform-kernel-01` 因沙箱阻止 sysctl、make 得到空内存值失败。7点检查确认已无旧进程后，用授权本机环境重试 `link-platform-kernel-02`。结果以对应 JSON/日志为准，不能提前宣称成功。

当前基础内核只证明编译成功；尚无实机内核入口、CPU/VM 初始化、根卷挂载或 launchd 证据。

## 7点续接结果

`link-platform-kernel-02` 已在授权本机环境真实完成，退出0，用时94.58秒。独立候选 `out/p4/kernel-platform/kernel.macho`：19261032字节，SHA256 `244706016549da5a9e3ef927a94faa564c93cb83a3322e8ec8252fb04003ab80`，ARM64 MH_EXECUTE，入口 `0xfffffff0073fc488`，未解析符号0。POST HMAC已按正常构建约定填充，运行自检未执行。

此候选的库已链接，但 KernelConfigTables 尚缺 ArmCpu/ArmDTPlatform 匹配项，ADT 缺平台驱动要求的 NVRAM proxy。不能因类符号存在就宣称驱动已经启动；尚未切换到自动进入内核的实机 profile。
