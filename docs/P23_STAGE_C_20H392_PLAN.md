# P23 Stage C: iPhone X iOS 16.7.16 / 20H392 + xnu-8796

日期：2026-10-27。参考 `ios-sweet` / negroni / guacamole / fajita NOTES-ios16。
本轮先下载和准备独立目录，不覆盖当前 P23 iOS16.7.11 候选。

## 目录与保护

- 新目录：`out/p23/ios-16.7.16-20H392/`
- 保留：`out/p23/ios-16.7.11/`、P21/P22、日志窗口。
- 不写设备、不写 userdata，直到所有 host gate 通过并单独授权。
- 不把 10063 的 MIG/display workaround 自动带入 8796；按 8796 原生语义重新评估。

## 输入

| 输入 | 来源 |
| --- | --- |
| IPSW | Apple iPhone10,3/10,6 16.7.16 20H392（已开始下载，6,171,035,648 字节） |
| OS/SystemOS | 20H392 IPSW 的 OS、Cryptex1,SystemOS |
| Kernelcache | 20H392 `kernelcache.release.iphone10b` |
| Trust | 20H392 StaticTrustCache / SystemTrustCache |
| Data | 20H392 Data seed，或按当前 seed 工具更新 build 20H392 |
| XNU | Apple `xnu-8796.141.3`（参考 negroni）+ UniXNU 4K 补丁重做 |

## Phase C0：IPSW 获取与校验

1. 下载 20H392 IPSW，记录 SHA256。
2. 解包 `BuildManifest.plist`，确认 identity iPhone10,6/d22ap。
3. 定位 OS、SystemOS、kernelcache、trustcache、Data 组件。
4. 抽取并校验 BuildManifest SHA-384。

Gate：IPSW SHA256/组件 SHA384 一致；磁盘空间足够。

## Phase C1：20H392 root / SystemOS / Data / trust

1. 用 20H392 OS/SystemOS 生成 SHNGROOT userdata 镜像；
2. 读取 root APFS Volume UUID；
3. 生成 20H392 static/system trust bundle；
4. 生成 20H392 Data seed（skip-setup、buildVersion 20H392）；
5. 记录窗口偏移、大小、hash、root UUID。

Gate：raw image 回读、APFS fsck、trust/root UUID 一致；不写设备。

## Phase C2：iPhone X ADT / identity

1. 生成 iPhone10,3/D22 ADT：
   - model iPhone10,3，target D22，product iPhone X；
   - artwork-device-idiom phone；
   - 1125×2436 logical canvas；
   - buttons/Home 参数按 iPhone X。
2. root-matching 指向 20H392 root UUID；
3. 保留 MSM8998 CPU/UFS/PSCI/display 硬件节点；
4. 不复制其他板的地址、UFS、触摸或 GPU 参数。

Gate：ADT decode/encode roundtrip；硬件节点 unchanged；identity 与 iOS root 同家族。

## Phase C3：xnu-8796 源与板级驱动

1. 获取 xnu-8796.141.3；
2. 将 UniXNU 4K 页、native timer、PAN、boot progress、late `zzy-`/`zzz-` 补丁 rebase；
3. 针对 8796/iOS16 头文件重新编译板级 archive：
   - ArmDTPlatform、ArmCpu、NativeUfs、SoftwareAES、
     AppleKeyStoreState、RamBlock、NativeLegacyDisplay、touch/keys；
4. 评估并移除仅针对 10063+iOS16 的 MIG 兼容补丁；
5. 保留 8 核/PSCI/日志/显示适配。

Gate：kernel compile/link pass；对象与 native kernel 无 10063 遗留符号混用。

## Phase C4：20H392 prelink / payload

1. 从 20H392 kernelcache 读取：
   - kernelcache SHA256、slide、early_security_init、roots；
2. 用 8796 native kernel + 20H392 kernelcache prelink；
3. 打包 payload：
   - kernel、ADT、trust、Data seed、unixnu.toml；
4. 复用当前 UEFI 2 GiB/window-range（仅当 8796 接受该窗口）；
5. 生成 boot image，记录 SHA。

Gate：payload manifest、prelink spec、root policy、trust 全部校验。

## Phase C5：一次实机验证

1. 恢复 20H392 root 到 userdata（单独授权）；
2. 一次 `fastboot boot`；
3. 读日志：
   - root/SystemOS UUID；
   - launchd；
   - display/selector；
   - Data/jetsam；
4. 失败只修一个变量，不循环。

## 复用边界

可复用：

- UEFI/DarwinBoot、2 GiB 窗口、板级硬件定义；
- UFS/CPU/显示框架、payload/日志/fastboot 流程；
- Data seed 工具、root auth 框架。

不可复用：

- 当前 10063 内核与 17.7.11 prelink/kext；
- 当前 17 root policy/trust；
- iPad7,1 identity / 2048×2732 canvas；
- P21/P22 iPadOS17 镜像。

## 当前状态

- 20H392 IPSW 下载中；
- 尚未改代码、未写设备。
