# P10 安全组件集成结果

## 已取得的实机证据

用户对 run `20261022T170103Z-8b0642d3` 的屏幕转录确认：

- `P10 AMFI registered: TrustCache interface=3`。
- Apple Mobile File Integrity 安全策略加载。
- `P10 Trust Cache Runtime initialized`。
- 两份外部 Trust Cache 模块加载完成。
- CoreAnalyticsHub 启动完成，随后进入 `kernel_bootstrap_thread: bsd_init`。

这是用户屏幕转录与实际二进制相互校核的证据，不是电子日志。Image4 单独注册行未被转录；原始 Image4 代码已执行，Trust Cache Runtime 实际返回，其所需 Image4 接口已可用。P9 的 `image4 interface not available` 不再是当前首个错误。

## 输入、架构与官方预链接

选用原始普通 ARM64 iPad7,1 / 17.7.11 / 21H461 的安全组件，限于内核安全 donor，设备目标仍是 OnePlus 5，未来用户空间目标仍是 iPhone iOS。没有套用 donor 硬件地址、分区或平台身份。

原始 kernelcache SHA256：
`f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`。

源 XNU 10063.141.1，donor XNU 10063.142.1.703.8，同属 Darwin 23 普通 ARM64，补丁版本并不完全相同。选定接口、绑定及实机安全初始化已有证据，不能扩展为所有 kext 和用户空间 ABI 均兼容。XR ARM64e 候选未修改 subtype，也未使用。

通过固定官方 NativePrelink 实际整合 AppleImage4、AMFI、CoreTrust、corecrypto 和真实依赖。原始执行字节保持不变。Image4 接口 20、AMFI TrustCache 接口 3 的真实表结构和导入绑定已检查。未编写替代 kext 合并器，没有空接口、占位指针或伪造验证结果。

两份原始 Trust Cache 与 donor 同源，bundle SHA256：
`7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7`。
输入摘要核验不是 Apple 个性化签名验证，不宣称拥有真实 Apple NVRAM 或安全身份。

## 对应真实错误的修复

1. 实际活动 RAM payload 驱动仍有旧大小上限。修改 DSC/FDF 真正选中的模块，重新编译并检查机器码；保留 FDT、内存归属、范围及 CRC 校验。
2. 原始 AppleImage4 报 `non-sensical crypto hash method`。实际字符串、指令、panic 导入与旧 ADT 一致；仅添加 NUL 结尾的 `/chosen/crypto-hash-method=sha2-384`。
3. 原始 AMFI 报缺少 `amfi-only-platform-code`。实际代码要求 4 字节；按固定官方实现添加 uint32 小端 1，保持仅平台代码策略。
4. 随后实际进入 BSD，并报 pthread 函数表为空。加入同一 donor 的原始 `com.apple.kec.pthread`，官方 prelink-v4 的依赖、代码和绑定检查通过。原有 KLDBootstrap 根据组件元数据自动加载，未伪造函数表。该新镜像下一次实机首错变为 PMGR；尚无 pthread 注册和初始化成功的直接运行证据，不能仅凭首错变化认定 pthread 全部初始化完成。

前两项 ADT 修复已被后续实机日志越过，安全校验代码没有改动。

## 当前首个错误：AMFI 无可用 PMGR 对象

当前 run `20261022T171445Z-5f7842cb`，首错：
`"AMFI: No PMGR?\n" @ConfigurationSettings.cpp:235`，
Caller `0xfffffff004e9c420`，Last bootstrap `bsd_init`。

实际 Caller 前一条 BL、格式串、源文件名和行号均与转录一致。原始 AMFI 等待 `ApplePMGRNub` 并进行类型检查，失败则 panic；精确结论是没有得到可用的正确类型对象，不能仅凭屏幕断言所有 PMGR 类都不存在。成功路径将 nub 的读值 OR 0x4 再调用写接口；其 provider 位于对象偏移 0x88，分派槽为 320/321。

已将固定上游只读审计适配到实际候选布局，所有原始指令、绑定和代码哈希断言保留，真实运行通过：
`scripts/p10-audit-pmgr.py`，
`out/p10/fatal-analysis/pmgr-contract.json`。

原始 donor 的 AppleT8011PMGR 对应槽静态行为是返回 0xffffffff、写立即返回；这不能证明 Snapdragon 上存在可用电源后端。上游 ShengPlatformPower 明确是生命周期/发布/拒绝测试工具，未提供真实 power backend。不能把它或伪造 VMM 属性当作本项目的硬件实现。没有加入 PMGR 空壳、绕过 AMFI、试探写硬件或复制 Apple/sheng 寄存器。

## 剩余边界

原始组件的 nonce slot、root power domain、SFR/manifest 缺项日志仍存在，不用猜测值补齐。NativeUfs 为避免与原始 IOStorageFamily 重复类而暂未链接；实机确认 NativeUfs 类不可用。尚无 UFS IOMedia、根卷、APFS、launchd 成功证据。

iOS 17 安全组件路线已有实际进展，不必仅因旧 Image4 fatal 转向 iOS 16。但普通 ARM64 iPhone iOS 17 用户空间、存储 ABI 和真实平台服务仍未解决。iPhone X iOS 16 / Darwin 22 仅保留作同源独立回退，不与当前 Darwin 23 混用。
