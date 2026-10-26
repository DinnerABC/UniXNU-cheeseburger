# P21 用户空间兼容性矩阵

更新：2026-10-26。下表分析基线为R17 D2；后续UP1已由用户确认出现原始界面和时间，当前触摸待适配，运行日志差异仍待回收。见P21_UPSTREAM_BOOT_COMPARISON.md。
R17 D2未达U4；UP1达到用户确认的可见原始界面里程碑，照片及精确帧数待补。不把进程存活、符号存在或主机检查记为功能完成。
完整12字段机器记录：`out/p21/userland-audit/compatibility-matrix.json`。

## 当前矩阵

| 模块 | 状态与真实证据 | 缺口及后续影响 | 最小处理/安全边界 |
| --- | --- | --- | --- |
| 用户管理 / usermanagerd | 部分验证：p89仍在运行；未复现早期persona-manifest SIGABRT；当前未记录直接错误 | 完整Persona inventory与每次初始化返回值未知；当前不是证实的首个阻塞 | 已复用；暂不修改；不得伪造persona成功 |
| Persona / XNU Persona API | 部分验证：原始persona/alloc/lookup已编入；有PERSONA_ID_501容器缓存；当前未捕获Persona错误码 | 原始System/Default/Personal persona清单未采到；尚不能排除某项身份依赖 | 保持原结构/编号，不复制其他版本；保持UID/GID与权限判断 |
| 数据保护 / AppleKeyStore | 部分验证：sel7=82/0,14=1/0,17=265/0,35=9/0；源码等于固定官方；记录的上述状态查询返回0 | 未按调用者证明MKBEventsRegister完成；无SEP密钥能力；对应上游同等待链的共同依赖 | 已接入；没有证明还需增加selector；只支持临时非密钥状态/事件；不宣称数据保护 |
| 文件系统 / APFS RAM Data | 部分验证：1GiB Data可写与服务/容器活动；偏好501:501/0600已验证；见过class2保护类别拒绝；非RENAME_SWAP失败证据 | rename/lock/fsync/xattr每条运行路径覆盖不足；不能把HFS案例套到APFS | 不重建Data；无失败调用不修文件语义；不绕过内容保护；无系统分区写入 |
| 容器 / containermanagerd | 部分验证：p64及p93运行；已返回多个PERSONA_ID_501容器路径；有部分system-group路径缺失日志，未定位为当前因果 | 所有必需组与ActivityKit容器未逐项证明就绪；可能影响服务初始化但非已证实根因 | 保留已验证目录；不预造任意容器；保持原Sandbox/Persona授权 |
| 基础服务 / securityd/configd/logd | 部分验证：最新快照均存在，分别p109/p37/p25；本轮未捕获对应明确首错 | 服务存活不等于全部XPC请求能回复；尚需端点及初始化条件证明 | 不批量禁用；保留原安全服务与AMFI |
| 图形服务 / backboardd/IOMFB/IOSurface | 部分验证：p57、display stage10/reg1/clients4；原始图形组件保留；selector15权限拒绝已有解释；swap check/reject/transfer全0 | 尚无本轮有效IOSurface提交及软件首帧；下游仍停在首帧提交之前 | 不放宽swap；不启用Metal；不改变调试许可或伪造完成 |
| 系统界面 / SpringBoard | 实际等待：p27/t417，Chrono订阅同步请求20份快照389..485s；无匹配致命退出；同步未回复 | 服务端及后续第一帧仍未知；直接阻挡当前界面启动路径 | 不修改原始SpringBoard；先修共同依赖；不篡改原始签名二进制 |
| 扩展服务 / chronod/liveactivitiesd | 实际等待/缺证：chronod p97 ActivityKit调用68份快照147..485s；liveactivitiesd p96存活；无本轮已捕获的MKBEventsRegister nil崩溃 | 服务端运行接收PID/请求队列及PushServer状态未证实；已定位SessionCore PushFrequentUpdatesObservationServer入口 | 继续静态解析PushServer和注册，不合无依据补丁；不返回空集合掩盖问题，不关闭daemon |

## 统一依赖与当前重点

原始launchd并行启动服务，不能预设严格串行。已知采样链为SpringBoard的CHSChronoServicesConnection订阅等待，以及chronod的ActivityKit同步预算查询。静态确认服务端SessionCore.PushFrequentUpdatesObservationServer.activitiesExceedingReducedPushBudget入口0x1c198ced4；对象偏移32为pushServer，方法经PushServer状态getter筛选集合。该getter含同步dispatch包装，实际队列与持有者需进一步确认，不能从代码存在就宣称实机已执行或死锁。

QEMU同版本明确记录同等待链最终由AppleKeyStore事件服务打通。本机已含相同事件服务并有返回0的状态查询，因此没有证据支持重复加入、任意扩展selector或再次重做persona seed。

当前不新增Persona/APFS行为补丁，不禁用chronod/liveactivitiesd，不修改原始签名用户二进制。未找到经核对的共同缺失接口；未制造“批量合并”成果。

## 本轮离线检查

`scripts/p21-check-userland-baseline.py`批量核对冻结镜像/内核/Data哈希、启动参数、偏好所有权、Persona/rename/fsync/KeyStore编译接口、原始APFS/pthread/Sandbox/图形roots和原始kernelcache。结果见`out/p21/userland-audit/baseline-check.json`。该检查只证明输入/接口一致性，不替代实机语义、密钥安全或桌面验收。

## 新确认的服务注册与状态路径

SessionCore原始cstring0x1c1a780c0为`com.apple.liveactivitiesd.xpc.PushFrequentUpdatesObservationServer`；服务初始化代码0x1c1a2c6c0..0x1c1a2c6ec使用该字符串，返回对象写入listener字段。`server-endpoint-0.asm`保留原指令。它建立静态服务归属，不证明本次运行注册成功。

服务端方法0x1c198ced4读取对象+32的pushServer；Swift字段描述符确认类型为SessionCore.PushServer。其metadata accessor0x1c1968e68指向metadata0x1f5513018；vtable+0xe8对应getter0x1c1923924；getter经0x1c798c6bc分支岛调用0x190994368同步dispatch包装后处理状态。仍未确定本次服务端是否进入getter、具体队列是否被占用，不宣布循环死锁。

此前从稀疏符号就近显示的ActivityOutputClient名字不应当作精确归属；此次以实际selector/字段/metadata/注册字符串替代该推断。
