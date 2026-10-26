# R17 D1 综合诊断版

## 基线与目的

用户要求继续从 R17 诊断，不采用 R18 回退。候选输出为
`out/p21/r17-diag1/`，不是 R18 的续编。

- Data 与 R17 逐字节相同，SHA256：
  `f1a23b6ba8e1a5ed13e1b3e36c3666e5eb76c3a2d54ac1e1803ff2b1e52a06c4`。
- ADT、TrustCache、原生显示验证/缩放/复制、HID、AKS 和 watchdog 行为保留。
- R17 原参数顺序及数值全部保留，仅增加 `p21-iokit-trace=1`、`kextlog=0xfff`。
- 不修改 selector15 返回值，不开放 debugger 许可，不启用 GPU 或关闭 AKS。
- 目标是区分渲染器初始化与系统服务等待；本版不宣称零帧已修复。

## 一次纳入的采集范围

| 范围 | 内容 |
| --- | --- |
| SpringBoard/backboardd | 每角色最多24个线程，每线程PC/LR、cache slide、16个返回地址、读取错误；独立worker采样，不停止线程 |
| IOKit | 原始单项/多项服务匹配、通知注册、调用结果，以及调用方实际遍历出的服务类；不重复匹配、不提前消费迭代器 |
| 匹配目标 | SpringBoard、backboardd、PreBoard、lsd、datamigrator及相关迁移/用户/容器进程；IOAccelerator/AGX请求与空结果有累计计数 |
| 显示 | 原始客户端selector/通知端口、vsync/prepare、显示状态字、registry属性、swap时间线及传输/拒绝计数 |
| 系统服务 | 同时记录24个关键进程，含迁移、PreBoard、用户会话、容器、安装、LaunchServices、logd |
| 平台 | UFS完整性/读取错误、RAM Data/激活、root认证/挂载、VM压力、PMGR/HID/按钮的已缓存状态 |
| 故障 | 原有退出记录、首个UI/watchdog fatal、内核消息及控制台尾部 |
| 官方汇总 | 从固定上游BootLog模板自动接入所有具有实际provider符号的只读报告；未选择的依赖保存在coverage清单 |

IOKit记录保留最初16条和最近112条；累计计数、覆盖及锁争用丢弃数同时报告。
`done=0`表示尚未记录完成，若有争用/覆盖，不能单凭它断定原调用仍阻塞。
多项查询返回迭代器不等于找到服务；`hits`来自调用方真实的迭代结果。
`done=1`为原始匹配调用返回，2为迭代取得对象，3为迭代结束；单项匹配
是否成功以原始rc及result类名判定。查询文本保留常用字符串键，不是完整字典转储。
不保留或解引用已释放的结果对象，只复制当时可用的类名与原始句柄。

线程采样是运行中的有界快照，可能因task/thread忙或地址变化失败；并非停止全机的
原子stackshot。调用栈必须用匹配21H461的符号解析，不能搬用sagit/iOS18地址。

## 官方来源与适配边界

流程依据固定 UniXNU `16e592abee8a550dec8154ed1f38b5ebc4f16213` 的
`docs/porting/log-capture.md`、`docs/guide/debugging.md` 与显示适配说明。

官方BootLog报告来自 UniXNU-sheng
`0c2046b84dbee545e7e3923eb74d7bb1ce020978`。原文件与该提交逐字节核对。
模板在 `OfficialLogReports.template.inc`，构建脚本扫描实际编译对象的定义符号，
生成 `OfficialLogReports.inc` 和 `official-log-coverage.json`。
当前33组中10组具备链接依赖；其余23组未选择。具备依赖不代表运行时一定产生内容。

硬件fence读取与GPU export区没有接入；本板无已选择的对应provider，不能复制
sheng地址或补空实现。数据和驱动导出的只读报告沿用，IOKit边界记录与线程采样
是显式的本板诊断适配，不冒称未经修改的官方功能。

## 存储与读取

只用原已授权、已备份的32MiB窗口：LUN0绝对LBA5062912起，共8192个4KiB块。
GPT、root和其他分区写入权限不变。

- 消息缓冲从128KiB增至1MiB；原XNU `CONFIG_MSG_BSIZE_DEV` 配置路径。
- SHLOG001 v3格式保留：msg_first=1、msg_blocks=256、slot_first=257、slot_blocks=32。
- 247个128KiB文本槽，每5秒提交，约20分钟40秒的环形诊断历史；超过后覆盖旧槽。
- 大消息快照拆成每次最多32块的写入，原128KiB命令上限与逐次读回校验不变。
- 新旧布局均由只读Recovery脚本校验；官方解码器已通过新布局和回绕测试。

回到Recovery后：

```sh
/opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py
```

一次输出原始窗口、msgbuf/slots/latest、读取回执和自动分析：
`DIAGNOSTIC_OVERVIEW.md`、`diagnostic-analysis.json`、
`ui-thread-stacks.json`、`iokit-matching.txt`。分析不自动断言根因。

## “全部日志”的实际边界

本版一次整合上述全部可用诊断，不再按单个组件逐版补充。但固定窗口和环形缓冲
不可能无限保存：读取回执报告覆盖数量；没有生成或没有转发到控制台/消息缓冲的
私有用户态统一日志、完整tracev3仓库、未编译硬件组件和最终未提交的panic片段
不在覆盖范围。原fatal屏幕路径保留，不在panic中新增未验证的存储写入或自动复位。

## 验证与运行证据

编译/链接、官方解码器回绕、存储命令边界、离线分析样本和R17输入对比是主机证据。
一次临时启动的回执独立记录在候选的boot-result.json；设备日志未取回前，不声明
线程采集/匹配跟踪已在实机成功，更不声明已经进入桌面。

## R17 D2 Chrono 服务端采样

2026-10-26，D1实际栈显示SpringBoard同步等待CHSChronoServicesConnection扩展订阅。
D2在原有S/B两角色之外加入C=chronod，每角色最多24线程、每线程16返回地址，
行缓冲统一扩大为360×91字节；不暂停线程。解析器增加C角色，fixture验证通过。
Data、ADT、AKS、启动参数沿用D1，无Metal/GPU发布或GPU搬帧开关。
这版用于确定服务端依赖，尚未证明Chrono是零帧唯一原因。
