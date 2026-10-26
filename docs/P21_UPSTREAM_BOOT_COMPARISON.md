# P21-UP1 启动对比

更新：2026-10-26。首轮B已临时启动，运行差异等待Recovery回收；未宣称补丁有效或U4成功。

B镜像：`out/p21/upstream-a/uefi/boot-p21-up1.img`，82583552字节。
SHA256：`7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`。
最终kernel SHA256：`bb711854df65705a7e5fb28f56ea132f183e3b84ad8aa8cbd9f05937d814f662`。

来源与门禁见P21_UPSTREAM_PATCH_EXPERIMENT.md。最终kernel包含参数名、两条新增作业和UP1代码标记；R17冻结镜像哈希未变。一次身份绑定临时启动，Sending/Booting均OKAY；无fastboot flash、无自动重试。回执`out/p21/upstream-a/boot-result.json`。

| 指标 | A：R17 D2 | B：UP1 |
| --- | --- | --- |
| 观察窗口 | 96份/485秒 | 等回收；不以传输代替运行 |
| 策略 | watchdog-only | 三规则已编入/参数启用，文件发布与launchd采用待查 |
| SpringBoard/chronod/liveactivitiesd | 存在 | 待回收 |
| Chrono订阅等待 | 20份/389..485秒 | 待同滑动地址归一化比较 |
| ActivityKit预算等待 | 68份/147..485秒 | 待比较 |
| DISPLAY | stage10/reg1/clients4/frames0 | 待比较 |
| SWAP | check0/reject0/transfer0 | 待比较 |
| UI/watchdog致命退出 | 无匹配记录 | 待比较 |
| 原始物理界面 | 未达U4 | 待用户/设备证据 |

只读Recovery回收：`python3 scripts/logging/read-cheeseburger-log.py`。
随后：`python3 out/p21/upstream-a/scripts/compare-run.py <回收目录>`。
脚本按不超过485秒窗口比较原始栈anchor/滑动、swap、进程和策略摘要，不仅比较条数/PID数。文件written不等于launchd接受，进程缺席也不能单独证明策略采用。

若运行无收益，标记tested-no-demonstrated-benefit，不长期默认启用，不再发同SHA。首选后续独立候选为核对maxvnodes17408与QEMU66560的差异，仍需结合内存/回收证据，不与UP1混测。coreduetd及其它作业不默认叠加。

## UP1 用户实机确认

2026-10-26，UP1临时启动后用户明确报告：“已开机，进入了系统出现了时间。”这确认本次物理屏幕出现原始iPadOS界面，构成定性的首个可见系统帧进展。尚未回收B持久日志，无本轮精确frames/swap/transfer计数，也无本轮照片；不据此断言已进入可交互Home桌面或长期稳定。触摸仍未适配。

补丁状态由pending改为`user-confirmed-visible-ui; retain-as-current-experimental-baseline`，先保留UP1作为后续触摸基线。由于没有单独拆分两条作业规则，不能归因到其中某一条。策略文件发布/launchd实际采用、Chrono和ActivityKit是否越过原栈，仍等待回收日志。不得再将UP1描述为没有物理界面证据，也不得把本轮用户确认写成照片验证。
