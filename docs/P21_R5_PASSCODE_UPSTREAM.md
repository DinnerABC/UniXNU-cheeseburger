# P21-R5 Enter Passcode：上游查证

2026-10-25，用户报告按键有反应，界面进入Enter Passcode。尚无该界面照片/完整提示，不能最终区分锁屏、升级确认及设置密码。未重启或改写运行设备。

## 本机证据

- R5继续使用R8 RAM Data seed，编码SHA256 0451a8ca7c3914112f3379549939512b9c735312202aa1ef59b82815554772e0；无持久化store。
- R8只读元数据after-readonly.json含keybags和keybags/backup目录，未列出keybag文件；未见passcode/springboard/purplebuddy/lockdown匹配文件。结果见out/p21/passcode-research/local-seed-check.json。这仅证明打包种子，不是读取当前运行RAM Data，也不能凭APFS卷未加密推论设备没有锁屏密码。
- R5 display archive含native_keystore_probe_stub.o，没有KeyStoreProbe.o/NativeKeyStoreEvents.late.o；配置没有sheng-aks。AppleKeyStore状态适配未启用。此前为避免元类注册panic保留此隔离；不能直接恢复曾有问题的archive。

## 官方同版适配

NativeKeyStoreEvents commit e59bac4ad1ac8825919ae94da8440a64d8ab90b3，实际读取NativeKeyStoreEvents.cpp及framework的KeyStoreProbe.cpp。

https://github.com/UniXNU/NativeKeyStoreEvents/blob/e59bac4ad1ac8825919ae94da8440a64d8ab90b3/NativeKeyStoreEvents.cpp

官方为ephemeral offline环境提供状态/通知服务，不提供真实密钥保护。源注释说明其目标libaks/MobileKeyBag查询：selector7 unlocked-since-boot位、17/35 DER设备状态；sls=3表示no passcode，0表示unlocked，二者不能混为一个状态。KeyStoreProbe支持sheng-aks=1、sheng-aks-sls=3，但还要求offline-key-events、ephemeral-storage及目标AppleKeyStore元类不存在；必须实际选择链接注册服务，参数本身不够。

## 同芯片sagit案例

固定commit898998cf1b74ea103e0c16d6dc0016822d8d644b：scripts/keybag_policy.py、tools/displayctl/keybag_aks_policy.S、keybag_data_policy.S。

https://github.com/MC-XiaoXiao/ios_sagit/blob/898998cf1b74ea103e0c16d6dc0016822d8d644b/tools/displayctl/keybag_aks_policy.S

其显式development-no-sep策略指出：不可用端点留下全零状态，iOS18部分直接AKS调用者把它解释成有密码。针对无保护开发device bag调整MKB/AKS查询，其他bag/无效输出保留原始路径。不实现密钥操作。该实现绑定22H374缓存SHA和偏移，不能复制到21H461。

另外docs/apfs/setup-skip-hardware-20261022.md记录Data设置向导完成偏好后出现桌面，这是Setup流程案例；不能将skip Setup与密码状态问题当作同一修复，也不能据此跳过当前密码提示。

## 判断和后续

优先候选是无SEP开发状态的兼容性，而非证实Data有旧密码。先确认完整界面文本，再针对21H461核对状态查询和原始返回值。优先复用官方仅ephemeral状态服务，并独立解决/核验元类注册生命周期，保留R5显示/输入基线。SystemOS没有证据需要重刷；若最终确需用户态补丁，必须对21H461目标重新验证，使用独立候选，不能直接使用sagit地址或数据。
