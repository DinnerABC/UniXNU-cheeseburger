# P21 官方流程审计（2026-10-26）

## 范围与证据

官方快照：UniXNU/UniXNU `16e592abee8a550dec8154ed1f38b5ebc4f16213`。入口 AGENTS.md 只有 `See docs.`，因此审计追踪了 building、porting steps/bring-up/device-tree/device-repository/migration/display/input/platform/log-capture、MobileGestalt、skip-setup、userdata、prelink、security 和相关 XNU 构建/参数文档。保存的 72 篇英文文档是检索资料集，**不代表每篇逐行完成审计**；主要结论来自上述关键流程及实际源码交叉检查。

固定版官方资料、组件源码比较、ADT 属性存在性记录保存在 `out/p21/r7/audit/official-workflow/`。这是新增审计目录，不属于先前生成的 R7 镜像 SHA256SUMS 覆盖范围；本目录独立生成 source-manifest.json。

对象：当前 R7 候选及其 R6 基线。R7 临时启动正在进行，本次没有重启设备、改镜像、改 Data、写分区或推送仓库。历史运行观测不能证明 R7 的最终状态。

## 核心结论

启动阶段顺序基本正确，但**尚未完整执行官方 OS 身份和首次启动准备流程**。不能把本端阶段构建脚本等同于当前官方 format 2 完整可复现端口。发现的配置缺口具有独立证据；尚无证据证明每个缺口都是本次零 swap 的直接原因。

### 1. 高优先级：平台 UUID 的输入缺失

实际 R7 ADT `/chosen` 没有 `unique-chip-id`（u64）、`chip-id`（u32）。官方 device-tree 文档明确要求两者用于生成 IOPlatformUUID。

交叉核对实际构建 XNU：

- `.work/p4-unixnu/.cache/xnu-work/iokit/Kernel/IOPlatformExpert.cpp` 的 ARM 分支读取上述两字段，生成 UUID；不是 x86 的 `/options/platform-uuid` 回退路径。
- `.work/p4-unixnu/.cache/xnu-work/bsd/kern/sys_generic.c:gethostuuid` 调用 IOBSDGetPlatformUUID。
- `iokit/bsddev/IOKitBSDInit.cpp:IOBSDGetPlatformUUID` 用 IOService::waitForService 等待 UUID 资源，零超时转成 NULL，即无限等待。
- R7 link.filelist 包含实际 IOPlatformExpert.cpo；所检查 DarwinBoot/板级平台代码中未发现另一处填入这两个字段。

**判断：确认缺少配置；具有源码依据的用户空间阻塞候选。尚未证明当前 SpringBoard/backboardd 正阻塞在 gethostuuid。** 需结合 R7 STK/FP 或后续资源就绪诊断确认。未来修复必须使用本项目可追溯、稳定的私有 seed，不借用其他设备 ID，不把 donor 的 Apple 芯片号当作 MSM8998 硬件识别值。

### 2. 高优先级：用户空间身份不完整，不能直接替换 model

R7 root model 仍为 OnePlus5，product 仅 name/model/product-name，没有 target-type、artwork-device-idiom 等完整 pad 身份。官方匹配 profile 和实际 donor 同为 iPad7,1/J120/j120ap、17.7.11/21H461。MobileGestalt 文档说明 DeviceClass 依赖产品名前缀，未知前缀可能落到 iPod。

同时缺少官方身份方案中的 `/chosen` 四个 activation/development 状态字段；没有 `/buttons`。其中按钮/手势选择应对应实际输入能力，不应机械复制无 Home 的平板配置。

**必须联动修复的本地依赖**：`drivers/CheeseburgerPlatformPower/CheeseburgerPlatformPower.cpp` 目前硬匹配 root model 的 OnePlus5 字节串。只改成 iPad7,1 会破坏本地电源适配匹配。应先把板级识别与 OS 产品身份解耦，保留经过验证的 MSM8998 compatible、CPU/GIC/PMIC 信息，再应用完整用户空间身份。官方特别要求 root compatible 保留真实板卡值，不能塞入 Apple 板卡 compatible。

身份更新后应处理 MobileGestalt 缓存；当前 RAM Data 每次重新创建，仍须检查种子是否携带缓存。此前 UI 几何异常与此可能相关，但没有证明其唯一因果关系。

### 3. 高优先级（直接进入桌面目标）：没有执行官方 skip-setup 和 no-auto-dim

当前沿用的 R8 Data seed 元数据清单中未见官方工具设置的 Setup/migration/SpringBoard 偏好。现有证据仅支持补过 Data 目录和权限，不支持已经完成官方跳过设置步骤。

官方 `scripts/seed-data-skip-setup.py` 对**种子的副本**合并：

- purplebuddy SetupDone/SetupFinishedAllSteps 等首次设置标志；
- migration LastSystemVersion 与 buildVersion=21H461；
- 对应首次启动偏好和容器目录；
- 可选 `--no-auto-dim` 设置三个禁止自动变暗/睡眠偏好。

官方文档明确说明，未具备完整按钮/idle 支持的板卡可能在 SpringBoard 启动后约 30 秒黑屏。它为此前短暂显示后变黑提供了合理解释方向，**不证明该次黑屏确实由这个定时器触发**。

此步骤不是每个 OS 都必须跳过设置，但与用户要求“直接进入系统”直接相关。当前 persistent Data 关闭，运行时设置/迁移成果不跨启动保留；只延长等待不会使下一次启动自动继承结果。

未来执行前核对版本、volume UUID、文件 owner/mode 和目录布局。官方脚本定义 MOBILE=501 但未显式 chown，实际挂载/写入权限仍须本机验证；不要假定工具运行成功即符合当前 seed 的所有权限要求。保留基础种子并记录新 seed/hash/skip_setup=true。

### 4. 中优先级：官方 OS profile 中有四条 text_fixups，当前没有对应 ADT 记录

匹配 profile 包含三条 dyld_shared_cache 子文件修补及一条 BTAudioHALPlugin 修补。R7 chosen 属性中没有 text-fixups。官方 payload.py 在 format 1 分支显式将配置转成 ADT 工具参数；不能仅添加 profile 名字就认定旧构建脚本已经传递。

**分类：确认 profile 与候选存在差异，语义及适用性仍待审计。** 本次没有逐一完成四个位置的符号、原始字节和当前运行路径确认，不能断言都必须套用或能解决黑屏。未来先识别函数/调用条件并验证原字节，保留签名及所有者策略证据。板级禁用 jobs 清单也必须按实际阻塞服务审计，不能整份搬用 sheng 清单。

## 已核实的正确步骤与边界

| 阶段 | 当前证据 | 判断 |
| --- | --- | --- |
| donor/OS ABI | iPad7,1 17.7.11/21H461；原始 kernelcache pin；prelink/接口审计 | 方向正确，不能改用小米6 iOS18 地址 |
| UEFI→DarwinBoot→XNU | 已达到 launchd、SpringBoard、backboardd；R4 曾显示升级 UI | 不是尚未完成固件 handoff |
| root/SystemOS | 历史部署读回记录、运行观察器 union/auth/rc | 有历史与运行证据；不意味着此刻重新完成磁盘读回 |
| Data | RAM Data、ephemeral-storage=1、独立种子 | 机制明确，但首次启动准备不完整 |
| display | 原始 ABI、晚初始化依赖、GOP 1080×1920、R4 缩放和实际 UI | 显示链曾工作；R6 零 swap 应检查提交前阻塞，不能据此重做全部扫描输出 |
| source-frame ownership | 本地保留 copy/retire 顺序及 overlay interlock | 仍须区分 rawFrames 与实际 SWAP/transfer，计数不能互相替代 |
| memory/CPU | 1 GiB、单 CPU 是当前可测试基线 | 官方未要求先 SMP；没有数据证明加核能解决等待 |
| HID/power | 物理 power/volume 读状态和 HID 有运行响应 | 部分支持；不是原生 Home、触屏或深睡眠唤醒完整实现 |
| 原始 pthread | 保留 donor 原始实现，排除重复源实现 | 这是已修回的互斥边界，不应为“正式化”再重复链接 |

## 官方组件并非都“旧”

对固定官方 catalog 的核心源文件逐字节比较：

- AppleKeyStoreState 42ff83e… 的 NativeKeyStoreEvents.cpp 与本地采用的 e59bac4… 完全一致，SHA256 `273b030afee897e48c30bc917c4df8a9c4d94a189d073ef80c0e0a15caef82ee`。
- ShengHidInput 5f3a37c… 的 NativeHidInput.cpp 与本地采用的 ba03e5d… 完全一致，SHA256 `0445e5f451ffcc827bdd603e822180444d998427fa3a6d63559b1a7b8f850998`。
- 官方 GopFramebuffer 的 reviewed revision 仍为 cf3b429…，与当前参考基线一致；本地板级缩放、输入和观察器改动需独立保留记录。

上述结论限定到比较的核心源文件，不代表两个仓库所有构建文件/包装器一致。rename 不是 runtime class/symbol 改名；官方 migration 明确要求保留 runtime_identifiers。

## 可复现流程与日志缺口

根 board.toml 仍是 example/P1 骨架，defconfig/feeds/锁文件未完整反映 P21 阶段脚本；没有达到当前 format 2 的 board+SoC+OS+arch、DTS、component catalog、ordered patch series、porting.toml 完整流程。现阶段主要依靠 out/p21/rN 门禁、脚本、hash 和实机回执。这是构建入口/维护缺口，不能直接等同于当前运行故障。

官方 migration 要求在现有端口原地保留硬件值、补丁、来源和证据后逐步迁移。不能删除已验证适配或直接选择其他 SoC 的完整组件集。Adreno740/SM8550、sheng 触控/背光/存储地址不适用于 MSM8998。

日志文档要求固件 QR exporter、kernel handoff、存储保留区 writer/reader、一次人工读取循环验证后才自动循环。目前蓝字/照片/手抄只覆盖部分，完整 userland 文本与持久 crash/log 尚不足。USB 已按用户要求暂停；本次不为补齐清单启用 USB 或存储写入，也不安排自动重启。CI/发布格式整理不会在用户未要求时触发 GitHub Actions 或推送。

## 后续建议顺序

1. 保留正在运行的 R7，先读取已增加的 FB entry/leave、AKS selector、STK/FP，确定目前卡在哪一层。
2. 下一候选优先补齐稳定 UUID 输入和资源就绪诊断；OS 身份与板级电源匹配必须成组处理，保留 CPU/内存/原始 Apple 代码边界。
3. 用官方工具在副本 seed 上准备匹配 21H461 的 skip-setup/no-auto-dim，验证权限、缓存、UUID 和报告，不改当前活动镜像。
4. 为 profile 的四条 text_fixups 建立符号/原字节/适用路径审计后决定是否合并，不盲修所有退出服务。
5. 另行整理 format 2 和 porting record，不用大规模迁移替代眼前的用户空间阻塞诊断。

这些是后续修复计划，本次只形成审计资料，没有制作或临时启动新的 R8。
