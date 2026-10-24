# P12 安装资格

**结论：不具备实际刷写资格。本参考镜像禁止刷入 OnePlus 5。**

iPad7,1 / 21H461 是用户明确限定的离线参考。即使所有构建与文件验证通过，也不能自动转为 iPhone iOS 安装候选。本轮没有执行 fastboot flash、erase、GPT 修改或临时启动。

| 门禁 | 当前结果 |
| --- | --- |
| 目标设备身份 | P3/P11 cheeseburger 历史证据；P12 未重新连接手机 |
| GPT / userdata 容量 | P3 原始备份 CRC 与哈希核验；9,382,658,048-byte 参考镜像小于 47,938,797,568-byte userdata |
| 关键备份 | P3 原有 manifest 和镜像保留；P12 对关键本地备份重新核验大小/SHA256，详见 root-integration.json |
| 完整 Android 恢复演练 | 未执行；不能仅由备份存在推断可完整恢复 |
| SHNGROOT 与整窗 SHA256 | 新参考文件通过；手机现有 userdata 未读取，未部署 |
| VerifiedRoot / UUID / ADT | 所有者签名和所有块通过；真实卷 UUID 与 ADT 一致 |
| Apple 安全验证 | 原始安全组件保留；未认证目标设备完整 Apple 启动链/根封印 |
| iPhone 正式系统兼容性 | 未解决；参考供体是 iPad，不能由系统通用 ProductName=iPhone OS 推断为 iPhone 固件 |
| NativeUfsBlock / IOMedia / APFS | 源码、静态 ABI、宿主及链接验证；没有 P12 实机证据 |
| 新 RAM payload | 约 119 MiB，已扩展有界协议预算；未证明手机 Fastboot 接收、UEFI 实际保留及装载成功 |
| 根卷 / BSD 后续 / launchd | 未实机挂载；未运行 launchd |

本轮打包的 `boot-p12-reference.img` 为 **131,112,960 bytes**，大于 P3 boot 分区备份所记录的 **67,108,864 bytes**。它是离线封装文件，不能写入 boot；Fastboot 临时下载上限和这一更大 payload 的设备装载能力也尚未验证。

## 未来独立安装流程必须满足的条件

1. 选择经过单独兼容性核验的正式 iPhone iOS 系统，建立与 XNU、原始安全组件、Trust Cache 和 APFS 内容对应的证据。不可混入 ARM64e 或不匹配版本。
2. 对该正式镜像重新生成 SHNGROOT、VerifiedRoot 策略、真实卷 UUID、ADT、payload 和完整 manifest；不得复用本参考镜像的哈希、UUID 或策略。
3. 只读重新核验真实手机身份、当前 GPT、userdata 容量、备份与设备模式。项目挂载故障时停止操作。
4. 关键唯一信息分区备份必须完整且可读取；另行验证 Android 系统/Recovery 恢复介质、机型、来源和恢复步骤。现有 boot/recovery/固件备份不等于完整 userdata 或完整 Android 系统备份。
5. 先验证更大的临时启动链和根设备发布能力，再决定独立的持久化测试。无电子日志/自动恢复能力时明确人工恢复条件，失联后停止重试。
6. 安装写入只能在未来用户单独授权、全部门禁通过后制定。不得写 bootloader、GPT、persist、modemst、fsg 等唯一信息和引导区域。

本轮不提供可直接执行的刷写命令。9.38 GB 根卷不能整体放入手机有限 RAM；本候选仅将内核和约 73 MB 校验策略放入 RAM，根卷作为独立离线数据文件。若未来使用更小的可信内存块设备，应限定为独立技术验证，不能冒称完整系统已部署。
