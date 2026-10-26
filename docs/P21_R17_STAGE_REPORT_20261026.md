# UniXNU-cheeseburger P21 R17 阶段总结

日期：2026-10-26（北京时间）
项目：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`

## 1. 当前结论

启动已到用户空间，SpringBoard、backboardd、chronod等服务能够运行；当前R17诊断基线没有记录到UI/watchdog致命退出，但首帧没有提交。尚未进入可用桌面。

已经从“黑屏、进程等待、怀疑GPU”推进到具体同步调用：SpringBoard等待Chrono扩展订阅；chronod同步调用ActivityKit的`activitiesExceedingReducedPushBudget`并等待回复。最新离线分析又定位到该方法的服务端实现，位于SessionCore的`PushFrequentUpdatesObservationServer`，尚未分析完服务端方法体与依赖条件，不能宣布根因和修复完成。

设备最近一次确认停在Recovery。用户本轮要求先总结，未进行后续启动或运行代码修改。

## 2. 当前约束与基线

- 官方文档和经过核对的上游实现优先。
- CPU渲染路线；不启用Metal、Adreno设备发布或GPU framebuffer搬帧。
- 1GiB内存、单CPU是当前基线；无证据证明增加CPU可以解决同步等待。
- 保留R17 Data、ADT、AKS与已经恢复的适配，不恢复被用户停止的R18旧Data/关闭AKS方向。
- 临时启动，不执行fastboot flash。唯一允许的设备持久写入为已经授权并备份的32MiB日志窗口；GPT、system/root等区域仍保护。
- 无自动重启/重试循环，无GitHub Actions、推送操作。

## 3. 已完成的主要工作

### 3.1 已确认并修正过的启动问题

以下总结来自当前项目记录，不能将所有历史版本都视为本轮重新验证：

| 问题 | 已实施处理及证据边界 |
| --- | --- |
| 早期PAN指令异常 | 当前构建关闭不适用的原生PAN配置；候选构建检查原生text中相关立即数指令不存在 |
| PE_create_console附近黑屏风险 | 启用官方DT panic region路径，避免沿用不属于本板RAM的模板固定地址 |
| SpringBoard主CADisplay缺失 | R14持久日志定位FrontBoardServices断言；R15恢复官方`offline-mobile-graphics-load=1`早期入口，之后UI致命退出消失 |
| watchdog隔离策略未执行 | R16将调用移出未启用的进度编译条件；实机观察requested=1/status=0/applied=1 |
| 照片/手抄日志不完整 | 已接入持久窗口、Recovery只读回收、原始日志保留及自动分析 |

### 3.2 R17 D1综合诊断

接入SpringBoard/backboardd有界用户栈、IOKit服务匹配及真实迭代结果、24项服务状态、原始显示/AKS/vsync计数和实际可链接的官方组件报告。没有改服务返回值或放宽swap。

D1回收：100次日志提交、写入错误0，原始记录位于`out/p21/persistent-logs/20261026T071852Z/`。确认SpringBoard的Chrono同步订阅等待，故在D2中定向加入chronod栈。

### 3.3 R17 D2定向诊断与启动

- 新增C=chronod；S=SpringBoard、B=backboardd保留。
- 每角色最多24线程、每线程16个返回地址，360×91字节行缓冲。
- 不暂停线程；保持try-lock与有界用户内存读取。
- 编译/链接、Chrono解析fixture、打包及输入基线检查通过。
- 镜像：`out/p21/r17-diag2/uefi/boot-p21-r17-diag2-persistlog.img`
- 大小82,587,648字节。
- SHA256：`e60f3d0823fbdf5089aa1659fe6c7d7c5b9ed8077dc9c5f90d02a1787da2e85a`
- 一次临时启动：Sending和Booting均OKAY；该结果仅证明传输及启动命令被接受。

## 4. D2实机回收结果

日志：`out/p21/persistent-logs/20261026T074907Z/`

- boot ID：`0x4dd61a51`。
- 96次提交，最后快照485秒，写入错误0，无槽覆盖。
- 原始窗口33,554,432字节，SHA256：`9bda32a5f71a4017c7dcbdd121faac890076c2100d91364a0d05c875c0282b8b`。
- DISPLAY stage=10、reg=1、clients=4、frames=0。
- SWAP check=0、reject=0、transfer=0。
- watchdog策略requested=1/status=0/applied=1。
- SpringBoard、backboardd、PreBoard、watchdogd未捕获匹配致命退出。
- chronod、liveactivitiesd都存在；单纯WAIT状态不等于异常，也不证明正常推进。

因此当前没有证据支持“swap被拒绝导致这次黑屏”或“CPU大量搬帧拖慢”；事实上尚未进入显示提交阶段。无panic/进程未崩溃不等于成功启动。

## 5. 已定位的具体等待与最新静态进展

### 5.1 SpringBoard侧

p27/tid417：`CHSChronoServicesConnection _subscribeToExtensions` → `BSXPCServiceConnectionMessage _sendSynchronously:` → Mach同步回复等待。

20份匹配快照覆盖389～485秒。不是整个485秒始终卡在同一函数；更早阶段有其他调用栈。

### 5.2 chronod侧

p97/tid592：ActivityKit → 同步BSXPC调用 → `activitiesExceedingReducedPushBudget`。

68份匹配快照覆盖147～485秒。返回地址runtime`0x2267cea1c`，共享缓存slide`0x9da8000`，unslid`0x21ca26a1c`。核对调用前ADRP/ADD和原始selector字符串，确认方法名；不是采用稀疏符号表的最近函数猜测。

### 5.3 本次总结前获得的新结果

复用官方ObjC元数据解析规则，修正共享缓存相对selector基址，并展开分类方法列表后，解析4个相关框架的14,002条方法记录，当前解析器异常计数0。

找到了服务端方法：

- 框架：`/System/Library/PrivateFrameworks/SessionCore.framework/SessionCore`
- 类：`_TtC11SessionCore36PushFrequentUpdatesObservationServer`
- 方法：`activitiesExceedingReducedPushBudget`
- 原始未滑动实现地址：`0x1c198ced4`
- 证据：`objc-methods.py`、`objc-methods.json`、`objc-errors.json`。

**这是服务端代码位置的确认，不是运行接收端PID的确认。** 下一步可直接分析方法体、内部队列/锁和初始化依赖，无需先再次开机。类位于SessionCore不自动等于证明本次请求由liveactivitiesd处理，也尚未证实循环死锁。

## 6. 上游对照结果

- 官方sheng采用Adreno740 Metal合成；不适用于直接照搬到MSM8998/Adreno540。
- 官方QEMU文档有无Metal时使用CPU渲染的路径，同时存在应用显示限制；不代表本机已经执行软件渲染器。
- ios-sweet有无GPU、反复加速器查找、随后CPU绘制迁移UI的案例；它另有高频完整帧拷贝占用单核的问题，不能解释本次零提交。
- 小米6固定资料曾记录加速器等待与后续DataMigration等待，但系统版本和ABI不同。
- 当前官方提交检索未找到这条Chrono/ActivityKit等待的直接修复；检索范围不等于全部仓库历史。
- 核对官方`d4c299b289b3`：停止coreduetd是由于持续高CPU占用，并伴随大小核调度调整，不是已经证实的本机修复，未应用。
- 当前系统profile的4条官方text_fixups已有配置/原字节核对记录，未发现漏其中一条。

## 7. 尚未解决的问题

1. 服务端方法体具体等待哪个对象/锁/队列，尚未完成分析。
2. 对应服务在本机是否完成注册与初始化，及本次请求接收端PID，尚未确认。
3. Chrono等待解除后是否足以产出软件首帧，尚未验证；可能仍有后续启动关口。
4. 当前未观察到原始软件光栅器完成真实一帧，不能把CPU拷贝配置或渲染线程存在当作产帧证据。
5. 本次没有形成已验证修复镜像，不能声称已经进入桌面或解决黑屏。

## 8. 下一步与停止条件

先离线反汇编`0x1c198ced4`，追踪原始服务端实现、调用分支及依赖，再核对镜像配置和本地IPC/唤醒实现。发现明确差异后修正，并保留来源与原字节证据。

不默认新增日志、禁用服务、返回空预算列表或开启GPU。只有静态分析遇到无法从代码确定的实际运行分支，才提出最小化运行验证需求。用户当前要求先总结，分析停在上述服务端入口，设备保持Recovery。

## 9. 证据入口

- `out/p21/r17-diag2/boot-result.json`：临时启动回执。
- `out/p21/r17-diag2/runtime-result.json`：D2回收状态。
- `out/p21/persistent-logs/20261026T074907Z/retrieval.json`：窗口读取和完整性记录。
- 同目录`decoded/cheeseburger-P21-4dd61a51/`：原始解码日志。
- 同目录`symbolicated-threads.json`：按同版本cache解析的线程。
- 同目录`wait-chain-persistence.json`：历史栈采样统计。
- 同目录`BLOCKING_CALL_FINDINGS.md`：客户端阻塞调用分析；该文早于服务端方法最新定位。
- 同目录`objc-methods.json`：最新服务端方法定位。
- 同目录`upstream-fix-review/FINDINGS.md`：上游补丁核对。

此报告不将主机检查、静态解析、Fastboot接受、用户空间运行、实际画面和可用桌面混为同一级证据。
