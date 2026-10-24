# P9 根因与修复记录

## P8 的确证范围

P8 修正版实测 run `20261022T111743Z-a9a520d5`，镜像 SHA256 `a4458921798976dba7e57f220f78163b488a18cb9d5da5e615c36770986629d2`。用户观察刷屏后仅留下 `MACH Reboot`，没有保存此前 fatal 正文。当前不能把 UFS、IOKit 或根卷认定为该故障原因。

固定 XNU 的 `osfmk/arm64/machine_routines.c:804` 在 `halt_all_cpus(TRUE)` 打印该行，然后调用 `PEHaltRestart(kPERestartCPU)`，若未返回则停在平台回调，若返回则最终循环。以下均是源码可达路径，尚未通过手机调用栈区分：

- panic wrappers → `panic_trap_to_debugger` → debugger diagnostics → `kdp_machine_reboot_type` → `PEHaltRestart` → `halt_all_cpus(TRUE)`。
- 非 panic 的 Debugger 处理也可能请求 `kdp_machine_reboot_type`。
- `host_reboot` 可以直接进入 `halt_all_cpus`。

`IOPlatformExpert.cpp` 的平台重启入口不保证存在能重启 MSM8998 的实现，因此打印重启请求不代表实际返回 Android。**第一条 fatal 正文目前尚待实机捕获，不能先写成已修复。**

## 官方接口核对

已阅读[调试文档](https://unixnu.com/guide/debugging)、[bring-up](https://unixnu.com/porting/bring-up)与[troubleshooting](https://unixnu.com/reference/troubleshooting)，并核对固定源码。记录见 `evidence/p9/debug-interface-check.json`。

`panic_screen=true` 的通用 payload 步骤调用设备覆盖脚本；本固定通用树没有该脚本，实现位于固定的 UniXNU-sheng 覆盖层。`--no-reset` 省略的是该补丁器自己的复位探针，renderer 会停住 fatal 路径；它不全局禁用 XNU 重启。对 P8 副本真实执行该脚本后，因 SCTLR/ISB/CPACR 入口序列不匹配而拒绝，原件保持不变。

`boot-progress=1/2` 需要 sheng 的 BootProgress 包，P8 未链接该包；不能仅加 boot arg 就声称存在。串口路径亦未证明，P3 没有 EFI SerialIO，当前没有已配置的手机 UART 电子日志。当前固定 pe_init 的 panic region 接口也不能替代经验证的保留内存；没有猜测 pram 地址或冒称跨重启 RAM 可回收。

## 本轮实际修改

复用官方 Panic Screen 的格式化、指针可读性检查及字形绘制代码，通过源码接入当前内核，避免地址硬编码、代码洞、额外入口色条和复位探针：

1. 在 `panic_trap_to_debugger` 最前面捕获 format、真实 Darwin va_list、caller、reason、可验证的异常 context；早于 sync、debugger trap、平台 callout 和嵌套 panic 检查。
2. 在 `kdp_machine_reboot_type` 记录独立的 debugger 重启分支；在 `halt_all_cpus` 保留没有经过 panic funnel 的主动 halt/reboot。两者不会被伪称 panic。
3. 首个 fatal 记录驻留永久 DATA，原子 first-wins。保存正文、原始 caller/最多 16 层有界 frame chain、可验证的保存态，以及 live ESR/FAR/ELR/SPSR/TCR/SCTLR。live 寄存器可能陈旧，屏幕明确区分。
4. 通过现有 bootstrap 日志 helper 保存最后阶段，仅补 IOKit 返回和 BSD 入口两处高层边界；没有几十个逐点 probe。
5. 使用 P8 已验证的 post-VM framebuffer VA，2 倍字形，首个 fatal 后屏幕保持、不再刷屏或重启。不能自动恢复；不继续运行未知状态内核，不进行任何存储/固件写入。

宿主测试执行真实 wrapper 及派生 formatter，验证格式参数、va_list ABI、无效字符串拒绝和字形边界；地址查询及显示硬件在该测试中是夹具。最终内核真实反汇编核对捕获分支和 DATA 生命周期。编译/离线检查不等于设备 fatal 已成功显示。

## 实机结果：panic 入口已捕获，缺少 Image4 的调用点已定位

run `20261022T123820Z-b6fabca3` 中，用户看到 `P9 FIRST FATAL / PANIC COMMON FUNNEL`，最后阶段为 `trust_cache_init`。原始转录保存在 `evidence/p9/first-fatal-user-transcript.txt`；这是用户屏幕转录，不是电子内核日志。没有正文、有效异常 context 或 LR 链；live ESR/FAR 为零不能说明没有故障。

OCR Caller 原文为 `0xfffffff007ala5b0`。将其中 `l` 解释为十六进制 `1`，得到候选返回地址 `0xfffffff007a1a5b0`。**以下定位以该 OCR 解释正确为条件**，尚不能冒称读取到了原始 panic 正文。

`scripts/p9-analyze-fatal.py` 对冻结的已测试内核执行真实反汇编和 Mach-O 字符串解析：

- Caller 是返回地址，应检查 `LR-4 = 0xfffffff007a1a5ac`，而不是把 LR 本身归给紧邻的 `_xnu_load_trust_cache.cold.1`。
- 该指令位于 `_trust_cache_runtime_init.cold.3`，实际 `BL` 目标是 `_panic`。
- 实际 ADRP/ADD 传给 X0 的字符串为 `image4 interface not available @%s:%d`。
- 固定源码 `bsd/kern/kern_trustcache.c:472` 对应 `img4if == NULL` 检查，与屏幕最后阶段一致。

指令、格式串和结果在 `out/p9/fatal-analysis/`。这是高置信调用点定位；不是一次硬件异常，不能从陈旧 ELR 推断 CPU fault。

P9 已直接区分出 panic common funnel。P8 的 `MACH Reboot` 位于 `halt_all_cpus(TRUE)`；panic 后经 debugger/平台回调进入该路径可解释现象，但没有 P8 原始调用栈，不能声称已逐帧还原 P8 重启链。

## 修复了诊断正文丢失，未绕过 Image4 检查

最终对象揭示派生官方 renderer 的内联汇编缺陷：Darwin 汇编器将分号后内容视为注释，`at s1e1r, …; isb; mrs …, par_el1` 实际只生成 AT。结果变量没有被 MRS 写入，导致指针检查错误拒绝正文和 frame chain。原宿主测试替换了 privileged readable 函数，未覆盖此问题。

现已使用显式换行分隔 AT、ISB、MRS，保留硬件可读性检查，并补齐 precision 参数读取保护。真实增量编译、链接和机器码回归通过：原测试对象 13 处查询均缺少完整序列；修正版对象和最终内核的 15 处查询均有 AT→ISB→MRS。原对象、测试镜像及精确输入均保留。证据：`out/p9/translation-regression/result.json`、`out/p9/kernel-check-v2/result.json`。

修正版内核 `out/p9/kernel-v2/kernel.macho` SHA256 为 `c27e26827ac646a2fc6adf39ae5e6b2a6cc614c36c67566480a3adaa541d87b6`；尚未打包和实机复测，不能声称正文/调用栈已在手机上修复。

## 当前真正阻塞：兼容的安全组件

当前内核的全部预链接组件段为空；Image4/AMFI 接口注册函数只是接口入口，不提供验证实现。现有 iPhone XR iOS 17.6 kernelcache 是 ARM64e / xnu-10063.142.1，当前为普通 ARM64 / 10063.141.1。

已实际运行固定 `NativePrelink` 提交 `2b6ffb50117eee820b98aca9bd51b5c7dfb2970e` 的解析器：它拒绝该 iPhone 输入，返回 `Only ordinary little-endian ARM64 is supported`。记录在 `out/p9/security-inputs/result.json`。不能修改 subtype、伪造接口或删除 trust-cache 初始化来继续。

因此，诊断缺陷已修复并编译，Image4 缺项本身尚未修复。继续启动需要与普通 ARM64 及所选 XNU ABI 匹配的真实安全组件，或连同 XNU/补丁迁移至经过核验的普通 ARM64 iPhone iOS 构建。没有混入 sheng 的平台身份、其他系统 Trust Cache 或当前不兼容的 XR 组件。
