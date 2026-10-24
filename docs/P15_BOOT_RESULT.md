# P15 构建与一次启动结果

**实机已越过原来的exec ENOTSUP45：原始类型3/4处理器真实注册、SystemOS挂载成功、PID1实际运行EL0代码，新的停止原因是mount PID3正常退出状态66。** launchd LC_MAIN首指令完成没有直接单步PC证据，不报告服务初始化完成。

## 交付产物（实测镜像保持不变）

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| `out/p15/kernel/kernel.macho` | 124964864 | `57e494d75f22d9a2a9a5d7a6640bd887689af53b1fa7f4f806606628e736ca7e` |
| `out/p15/uefi/boot-p15-original-exec.img` | 81305600 | `6b452719a182e171ba3004ea932f54fd778c605b34ab541fd9c542373af3565f` |
| `out/p15/payload/payload.container` | 75179180 | `517d882b6ae9228339272184a4587fdf2b6ba2ef5d65b7b86d3a89e376be6ab9` |

展开bundle为125050880字节，SHA256 `d334a74622e59d1e292106990a061c7d9c70a95c66839ba715c025ba14c6e975`。沿用P13/P14压缩协议、固定EDK2解码器、地址参数、DTB/ADT及原始Trust Cache。19个原始Apple组件，所有原始可执行extent保持。离线编译/链接、原始指针/入口、15项RootAuth测试、55文件CodeDirectory数据块及篡改负例、固定解码器负例、FV身份和镜像回读均通过，详见offline-gate及日志。

## 一次真实启动与照片

用户恢复原Recovery后核对固定序列、root、cheeseburger，转入Bootloader核实msm8998/unlocked。2026-10-23 17:06（Asia/Shanghai）唯一一次fastboot boot成功。没有flash、erase、GPT或任何分区写入；无自动重试。后续用户照片保存为 `out/p15/evidence/mount-child-exit66.jpg`，转录及符号核对在 `screen-result-01.json`。USB离线不用于证明执行；以下仅依据照片与固定代码核对。

| 实机字段 | 值与含义 |
| --- | --- |
| Apple / owner / accepted | 80 / 1 / 1；原始Apple EAUTH保留，owner成功 |
| 根卷身份 | readonly=1，三份dev_t均0x0100004f，matches=1，depth=9，UUID精确匹配 |
| 类型3 | status0，reg=0xfffffff00638fee0 |
| 类型4 | status0，reg=0xfffffff00638ff10 |
| 原始fetch | 两者均0xfffffff005efca54，与原始代码及静态重定位一致 |
| 首个exec错误记录 | errno0、函数/文件为空；此次未再遇到已记录的errno45 |
| SystemOS | stage6/status0/mount0/auth1，真实挂载/授权检查通过 |
| PID1 EL0 | 747次syscall；首PC0x100a99fb4，PSTATE0，syscall74 |
| 真实签名缓存 | 原始块201083，native4K页面检查2189，ready blobs53 |
| 首个fatal | mount[3]，signal0，exit status66，无CS_KILLED标记 |

## 九项结论与证据边界

1. **ENOTSUP45来源**：缺少原始类型4处理器的机制由文件xattr、XNU分支和P15修复后结果强力支持。P14内部首次返回45未直接跟踪；P15已实机越过该阻塞。
2. **解压注册**：已真实注册。两种status及实际表/fetch指针精确匹配原始组件，不是字符串推断。
3. **首个失败函数/码**：本轮直接捕获的是mount的exit syscall路径，经 `_exit_with_reason → _proc_prepareexit` 因状态66触发panic。mount内部最初失败API/errno仍未知，66不是exec errno。
4. **Mach-O**：已执行真实用户空间，且mount PID3也进入退出syscall。末态exec stage3/header/entry是全局字段，会被子进程覆盖；不能把它当作PID1 execve原始返回值。没有独立保留的PID1 execve返回0记录。
5. **原始dyld**：PID1首EL0 PC与同哈希原始dyld `_mprotect` SVC后的0x3afb4唯一匹配，syscall号74吻合；推导slide0x100a5f000。强证据支持原始dyld实际运行；基址来自匹配推导，没有独立map读取。
6. **launchd LC_MAIN**：启动行为已推进到原始mount进程，与上游launchd启动阶段一致；未采集LC_MAIN处EL0 PC、原始字节及首指令完成后PC，严格保持“直接执行证据未完成”。launchd完整服务初始化也未完成。
7. **SystemOS / Sandbox**：SystemOS实机成功；53个签名缓存blob及实际页面检查支持真实cache读取。Sandbox/AppleMatch原始闭包已预链接，后续mount行为与越过已知libSystem缺policy拒绝阶段一致；没有独立Sandbox handler返回0或shared_region返回0记录。
8. **新阻塞**：确定为mount正常退出66。上游相同阶段捕获过缺boot-manifest-hash；当前候选确实缺少该属性和发布器，因此是首要下一假设，但本机argv/stderr未捕获，尚不能确认mount内部根因。不能预填为缺Data或签名kill。
9. **userdata**：仍无需修改，且完全未写。OS/SystemOS、安全策略和P14冻结产物继续复用。

## 后续离线准备

官方owner boot-manifest发布器及调用点、PID1记录范围修正已在 `out/p15/next-boot-manifest/` 独立编译通过，未链接/打包/再次发机。它从两份真实已认证策略派生有明确owner域的SHA384，拒绝覆盖已有firmware身份；不是伪造Apple签名或identity。下一轮仍须先采集mount argv/stderr、首次失败API，以及严格LC_MAIN执行证据。

Data ramdisk仍未集成。此参考用户空间是iPad7,1 / 17.7.11 / 21H461，不代表最终iPhone iOS或完整UI完成。构建VM已停止，两次只读离线镜像检查均在结束后卸载。
