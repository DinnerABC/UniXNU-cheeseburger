# P10 真实构建与启动结果

## 当前结果

原始 AMFI 接口 3、Trust Cache Runtime 和两份外部 Trust Cache 已获实机屏幕证据；进入 BSD 初始化。当前仍停在 First Fatal，最新错误为 AMFI `No PMGR?`。没有启动 iOS 用户空间。

所有启动均使用 fastboot boot，不刷写、擦除、解锁或修改手机分区。完整构建日志、退出码位于 `out/p10/logs/`，每轮设备记录位于 `out/p10/device-loop/`。没有 GitHub Actions 运行声明。

## 关键实机迭代

| 镜像 SHA256 前缀 | 实际结果 | 证据 |
| --- | --- | --- |
| 8425902f | Shell；实际活动 RAM 驱动旧尺寸上限导致载荷未接入 | 用户反馈、驱动反汇编 |
| d99a5c42 | 原始 Image4 执行，因缺少 crypto-hash-method panic | user-report-01 与真实调用指令 |
| e83d925c | 越过算法选择，AMFI 因缺少 4 字节策略属性 panic | user-report-02 与真实 AMFI 读取代码 |
| 55b80dde | AMFI/Trust Cache 成功、进入 BSD；pthread 函数表为空 | user-report-03 |
| 5923c72c | 加入同源原始 pthread 后，首错为 AMFI No PMGR | user-report-04 与 PMGR 审计 |

部分轮次因用户错过画面而明确授权观察重放；每次均保留独立 run，不把重复转录当作新测试。USB 不可见的两次发送尝试 attempts=0，未发送镜像，不算内核启动失败。

## 最新已测试候选

- 内核：`out/p10/kernel/kernel.macho`，51503104 字节。
- 内核 SHA256：`684e673c783556a629bc5f55f05075770ef4538653331d1974c948e8d42a9ce5`。
- 镜像：`out/p10/uefi/boot-p10-xnu.img`，57708544 字节。
- 镜像 SHA256：`5923c72ce73bf8ce3b0eb6c66c9870fbb23baa3b8e339bf1562ec2e4a2b8480e`。
- 实际发送 run：`20261022T171445Z-5f7842cb`，temporary-boot 退出 0。
- 控制器结果：`NO_TRANSPORT_MANUAL_RECOVERY`，观察超时后停止，没有自动复位能力。
- 用户当前 First Fatal：`AMFI: No PMGR?`，Caller `0xfffffff004e9c420`，Last bootstrap `bsd_init`。
- 经过真实原始组件预链接、依赖/绑定/执行字节检查、DarwinBoot 宿主解析、增量 UEFI 编译、镜像回读和身份/备份门禁；这些通过不代表 PMGR 错误已修复。

版本已冻结到 `out/p10/milestones/v5-pthread-pmgr-panic/`。安全链成功并出现 pthread panic 的前一版本保存在 `out/p10/milestones/v4-security-pass-pthread-panic/`。

## 已确认与未确认

已确认原始安全组件开始执行、AMFI 接口注册、Trust Cache Runtime 返回、两份原始外部模块加载、CoreAnalyticsHub start 和 BSD 入口。尚未直接确认 pthread 注册、所有 IOKit 驱动完成、UFS 块设备发布、APFS 根挂载或 launchd。

最新 PMGR panic 可能来自异步 AMFI 工作路径；Last bootstrap 只是最后记录阶段，不能据此将所有 BSD/pthread 步骤判为已完成。live EL1 寄存器未验证且含 OCR 错误，不用其推断新的 CPU fault。

电子内核日志回收仍未实现；证据为用户屏幕转录加实际二进制核对。保留现场后需要人工长按电源恢复，失联后不得重复启动同一候选。

## 当前停止条件

没有合格的 MSM8998 PMGR 平台实现可用于修复本次拒绝。固定上游审计通过，只证明原始接口与分派，不证明硬件可用。未通过 PMGR 测试工具、VMM 声明或猜测 MMIO 写入制造表面成功；因此未生成冒称修复 No PMGR 的新镜像。
