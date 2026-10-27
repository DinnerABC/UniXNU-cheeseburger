# P23 执行结果：iOS 16.7.11 整合候选、userdata 部署与一次临时启动

更新：2026-10-27
设备：OnePlus 5 / cheeseburger / 序列号 `3b25cc7c`

## 结论

本轮已经完成上一轮列为“需要授权”的启动输入更新，并把它们实际打进一个整合候选：

- 新 ADT：root-matching 改为真实 iOS16 OS UUID，移除 17.7.11 专用 text-fixups，
  保留 PSCI、CPU 拓扑、UFS、显示和本机硬件节点。
- 新内核：用新的 `libVerifiedRootPolicy.a` 重新链接 native kernel 和 prelink，
  内核内嵌的两个 owner 清单与 20H360 OS/SystemOS 一致。
- 新 payload：实际装入 iOS16 trust bundle 和 **所有权修复后的 20H360 Data seed**。
- 新 userdata：OS/SystemOS 窗口已写入手机 userdata，备份和回读均通过。
- 一次身份绑定的 `fastboot boot` 已发送并返回 `Booting OKAY`。

**实机最终确认到的阶段是 fastboot 传输与启动入口接受。** 第一次启动（镜像
`c1f1ef78…`）在屏幕上停在 `rootvp not authenticated after mounting`；读取窗口仍是
R7 的 `boot_id 0x376cf54a`，说明该轮在持久日志启动前就停止了。根因已定位并修复：
`RootAuth.cpp` 以及构建树中的 `kern_exec.c`/`vm_unix.c` 仍硬编码旧 OS/SystemOS UUID，
导致 owner 根认证查询匹配 0 个 IOMedia。修复后重新生成镜像 `65f19475…` 并再次只发送一次 `fastboot boot`，返回
`Booting OKAY`；该轮越过了根认证，进入 launchd，随后暴露 `iniproc` 继承端口
错误（见 4.1）。再次修复后生成镜像 `6e3c6bf9…` 并发送一次，USB 再次消失，
屏幕/持久日志尚待确认。

上一轮静态分析中的三条过度结论已修正：

- Darwin 23 内核 + Darwin 22 用户空间是 ABI 风险，不是“已证明不能启动”。
- iPad7,1 身份与 iPhone 系统不同是需要实机验证的风险，不是字符串不同就必然失败。
- 旧 text-fixups 4/4 不匹配只证明旧偏移不能复用；本轮已移除旧条目，没有为了
  “补齐数量”强行写 20H360 补丁。

## 1. 实际修改的启动输入

| 输入 | 新值 | 校验 |
| --- | --- | --- |
| ADT | `adt/kernel.macho.adt`，14,208 字节 | SHA256 `46de8b23148110a83ff135758558de224057381bb04198eef0d840491eba9e36`；root-matching `E652776C-E75D-4712-850F-AB4D81C3D836`；旧 text-fixups 删除；其余节点逐项比较不变 |
| 内核（最终） | `kernel/kernel.macho`，117,796,864 字节 | SHA256 `daa5b742d5322d02d75b0e695b207df222868bd69b57d645756c8cf34e0077cf`；内嵌 OS 清单 44,008,256 字节/1,375,241 块，SystemOS 21,430,816 字节/669,696 块，逐字节匹配；最终内核中旧 `04769D26`/`8846FE78` 字符串为 0；公钥 `07680d33...c1d45f20` |
| owner 策略库 | `policies/libVerifiedRootPolicy.a`，62 MB | SHA256 `e6de3b3beb1a5bfe75802bccd92f5a9856814d7a0c4a381f7d838cdbce76923e`；原始 C 验证器全块通过 |
| trust bundle | `trust/ios-16.7.11-static-system.bundle`，67,764 字节 | SHA256 `7a68b36c...dc680a`；覆盖 20H360 launchd/dyld |
| Data seed | `data-seed/skip-setup-1g-d242-owned.dbr`，2,105,440 字节 | SHA256 `b45fe94f74270878336c6c78efd7e9bfaf0242a7524d8723fb4df89fd9776428`；UUID `D2420479...`；fsck 0；所有权已修复 |
| payload | `payload/payload.container`，69,256,377 字节 | SHA256 `76e39c5f...1ab7bf`；展开 120,000,512 字节 SHA256 `645617b5...45558c` |
| UEFI FD（最终） | `uefi/MSM8998PKG_UEFI.fd`，6,291,456 字节 | SHA256 `869ef9b54566265be2bd3a50158759798c95b392031676bc49e3f2f50ca58a2e` |
| 启动镜像（最终） | `uefi/boot-p23-ios16-8cpu-1g.img`，75,407,360 字节 | SHA256 `f0af5e2da1dd4f1146bc1ca76045649c170a7898d3b89c927704db4c5d39d447` |

内核代码没有改写；变化来自用新 `libVerifiedRootPolicy.a` 重新链接 native kernel、
重新 prelink、再打 panic-screen 补丁。8 核、1 GiB、PSCI、Normal NC 帧缓冲映射、
vnode 33792 和全部日志/观察器代码保留。

最终 boot image 的 payload 已独立解出并核对：kernel、ADT、unixnu.toml、scratch-data、
trustcache 五个文件的 SHA256 与 payload manifest 完全一致；因此新策略、新 trust
bundle 和新 Data seed **确实进入了最终镜像**，不是只存在于目录里。

## 2. Data 内容与所有权

上一轮的 `fix-ownership.sh` 有 `IFS="\t"` 错误，且 19 个 0700 目录无法作为普通用户
遍历。本轮用管理员权限执行了 `copy-var.py`：

- 以 root 挂载 20H360 OS 卷和 seed，使用不跟随符号链接的逐路径复制；
- 复制 `/private/var` 顶层 26 项（排除 `staged_system_apps` 和主机 `/tmp` 递归）；
- 源路径 196 条，逐条恢复 uid/gid/mode；复制 36 个普通文件、159 个目录、1 个符号链接；
- 复制后 `missing=0`、`metadata_mismatch=0`；
- 重新设置 `tmp` 0:0/1777、`run` 0:1/775、`mobile` 501:501/711，以及五个
  Setup 偏好的 501:501/0600；
- 展开后 `fsck_apfs -n` 退出 0，卷 UUID 仍为 `D2420479-F58E-44C5-AB79-1815EC32C686`；
- 最终 seed 217 条路径，`ownership_preserved=true`。

`ownership.tsv` 的解析也做了核对：9,610 行、每行 5 个 Tab 字段，路径中的空格保留；
脚本已改为真正的 Tab `IFS`（文件里是字面 Tab，不是 `\t` 两字符）。

## 3. userdata 部署、备份与日志窗口

- 部署前从设备回收 R7 日志：
  `out/p21/persistent-logs/20261027T005725Z`，boot_id `0x376cf54a`，
  158 次提交、write_errors=0；online=8，CPU1–7 step=7/fail=0/PSCI=0；
  DISPLAY clients=8 frames=5；SWAP check=5 reject=0 transfer=5；
  R7 的 17.7.11 text-fixup 实际应用；触摸仍 `error=-54`。
- 备份 `[0, 9,382,658,048)`（8948 MiB）：
  `backup/userdata-before-8948MiB.img`，SHA256
  `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`，
  与 P12 原始根镜像一致。
- 写入新镜像 7990 MiB 时，首次 `exec-in dd` 返回 0，但设备端分块核对发现
  **最后 54 MiB 仍是旧 P12 数据**（写入在 7936 MiB 处提前结束）。随后单独重写
  `[7936, 7990) MiB` 并验证尾部 SHA256 `fec206...138c67a`。
- 最终设备端整段 7990 MiB SHA256：
  `f8bf26f4dda809a38f1fbd8e99bd1082fa045119ba3cb09afd5675c77744314b`，与主机镜像一致。
- 日志窗口 `[10 GiB, 10 GiB+32 MiB)` 未被覆盖，设备端 SHA256
  `cae893710a98e46cb7ce593827f9a73b58b5a8e068a73328103d69a691873842`，与部署前回收值一致。
- 没有写 GPT、boot、recovery、固件或任何非 userdata 分区；`fastboot flash` 次数 0。

记录：`deploy-result.json`。

## 4. 一次临时启动

- `adb reboot bootloader` 后确认 `product: msm8998`、`unlocked: yes`、
  `max-download-size: 805306368`。
- 第一次 `fastboot boot` 使用镜像 `c1f1ef78…`，发送 73,620 KiB，返回
  `Booting OKAY`。屏幕实际停在 **`rootvp not authenticated after mounting`**，
  这一轮即首个具体错误。
- 读取窗口仍是 R7 的 `0x376cf54a`（158 次提交），说明失败发生在持久日志启动前，
  因此没有 P23 轮次的窗口日志。
- 根因：`package/BoardRuntime/RootAuth.cpp:80` 仍用旧 OS UUID
  `04769D26…`；同时构建树 `bsd/kern/kern_exec.c`、`bsd/vm/vm_unix.c` 的
  owner cache/root UUID 仍是旧值。`verifiedVolumeQuery` 因此匹配不到新
  `E652776C…` 的 IOMedia，`p14RootDecision` 的 `verified` 为 0，owner 根认证失败。
- 修复：`RootAuth.cpp` 改为 `E652776C…`；`kern_exec.c` 的 boot manifest/root 引用和
  `vm_unix.c` 的 root/cache 引用改为 `E652776C…`/`6B06A0EF…`；把
  `XNU_OWNER_CACHE_VOLUME_UUID` 设为 SystemOS UUID 并强制重编 `kern_exec.o`、
  `vm_unix.o`。最终 native kernel 中旧 UUID 字符串为 0。
- 第二次只执行一次 `fastboot boot`，使用最终镜像 `65f19475…`，发送 73,628 KiB，
  `Booting OKAY [10.094s]`，退出码 0。
- 记录状态：`TRANSPORT_ACCEPTED_SCREEN_PENDING`，`boot_commands=1`，
  `partition_writes=0`。发送后 USB 再次消失；第二次启动的屏幕和 P23 持久日志
  尚待确认。不能把 `Booting OKAY` 写成内核或用户空间成功。

记录：`boot-result.json`。


## 4.1 第二个用户空间错误：launchd `iniproc` 继承端口

根 UUID 修复后的镜像 `65f19475…` 实际越过了根认证，进入 launchd，但停在：

`pid 1 exited -- exit reason namespace 7 subcode 0x1, description could not set up inheritance port`

P23 持久日志：`out/p21/persistent-logs/20261027T020429Z`

- boot_id `0x3a73fcb5`，34 次提交，write_errors=0
- 根认证/挂载已通过：SystemOS `6B06A0EF…`、OS `E652776C…` 均挂载
- `P17 Data activation: result=0`，OwnerData `D2420479…` 挂载成功
- `load_init_program` 加载 `/sbin/launchd`，说明内核已进入 pid 1
- `shared_region: [1(launchd)] check_np(...) vm_shared_region_start_address() failed`
  在 R7 的 17.7.11 launchd 上也存在，**不是本轮独有致命点**
- 104.7 s 时 launchd 以 namespace 7/subcode 0x1 退出；此前有 launchd 每秒约 8076 次
  wake 的 watchdog 记录
- DISPLAY `clients=0 frames=0`，SWAP 全 0，尚未进入图形提交

定位 launchd 0x100016dc4 附近：

- 先 `task_set_special_port(TASK_BOOTSTRAP_PORT, MACH_PORT_NULL)` 成功
- 创建 bootstrap 端口后调用 `mach_ports_register(task, &port, 1)`
- 返回非 0 即跳转到 `"could not set up inheritance port"`

XNU 10063 的 `_kernelrpc_mach_ports_register3()` 对
`ports[i]->ip_immovable_send` 返回 `KERN_INVALID_RIGHT`。iOS 16.7.11 launchd
注册的是 immovable send right；iOS 17 launchd 不再走同一路径。这是确定的
内核/用户空间行为差异。

修复：`osfmk/kern/ipc_tt.c` 中对当前 task 允许 immovable send right
（其他 task 仍拒绝），并重新编译 `ipc_tt.o`、重链内核、prelink、payload、UEFI。

该轮镜像 `6e3c6bf9…` 仍报同一错误。随后完成 4.2 的 MIG 协议兼容修复，生成
`4b90271f…` 并再次只发送一次 `fastboot boot`；USB 消失，屏幕/新日志尚待确认。
**不能把这次发送写成 launchd 已成功或界面已出现。**


## 4.2 协议层根因与本地兼容修复

最新启动 `0x3a8107ab` 仍报同一 `iniproc` 错误。上一轮对
`_kernelrpc_mach_ports_register3()` 的 self-task immovable 例外没有解决问题，原因现已
由上游源码和实际 MIG 反汇编闭环：

- XNU8796 `task.defs`：`mach_ports_register(target_task, init_port_set:
  mach_port_array_t, init_port_setCnt)`。MIG 生成的请求是
  `descriptor_count=1` 的 `MACH_MSG_OOL_PORTS_DESCRIPTOR` + NDR + count。
- 当前 XNU10063 内核的 `task.defs` 服务器分支是
  `_kernelrpc_mach_ports_register3(target_task, port1, port2, port3)`，MIG 请求要求
  `descriptor_count=3`、`msgh_size=0x54`、三个 inline
  `MACH_MSG_PORT_DESCRIPTOR`。
- XNU10063 的 `libsyscall/mach/task.c` 自己把数组打包成三个端口，再调用
  `_kernelrpc_mach_ports_register3`；iOS16 launchd 走的是旧的直接数组 MIG stub。
- 因此 XNU10063 的 `__MIG_check__Request___kernelrpc_mach_ports_register3_t` 在
  descriptor_count/size 检查处直接返回 `MIG_BAD_ARGUMENTS (-304)`，根本到不了
  上一轮修改的业务函数。self-task 例外因此未生效。

本轮改动：

1. 在**生成的 task server** 中把检查扩展为双布局：
   - 新版三端口布局：原检查完全保留。
   - 旧版 OOL-port-array 布局：要求 `descriptor_count==1`、
     `msgh_size==0x40`、`MACH_MSG_OOL_PORTS_DESCRIPTOR`、disposition 17、
     `count==init_port_setCnt` 且 `count<=3`。
2. 新增 `ios16_mach_ports_register()`：把旧数组逐项转成三个端口，调用现有
   `_kernelrpc_mach_ports_register3()`；保留 `TASK_PORT_REGISTER_MAX`、immovable
   send 拒绝、task 空/非 active 检查；成功后才释放旧 MIG 数组存储。
3. 撤销上一轮未论证的 self-task immovable 放宽；旧、新协议都走原始权限检查。
4. 生成器补丁保存为：
   - `scripts/p23-patch-task-server.py`
   - `scripts/p23-mig-wrapper.sh`
   - `out/p23/ios-16.7.11/scripts/fix-xnu-root-uuids.py`（含 ipc_tt 兼容函数）

`mach_ports_lookup` 的旧/新宽度也一并核对：旧 8796 使用 OOL 数组回复，新 10063
使用三个端口；本轮先解决 launchd 的首个 register 阻塞，lookup 作为下一个明确的
兼容检查点保留，不做未经证据的放宽。

最终镜像：

- `uefi/boot-p23-ios16-8cpu-1g.img`，75,386,880 字节
  SHA256 `f0af5e2da1dd4f1146bc1ca76045649c170a7898d3b89c927704db4c5d39d447`
- `kernel/kernel.macho`，117,796,864 字节
  SHA256 `daa5b742d5322d02d75b0e695b207df222868bd69b57d645756c8cf34e0077cf`
- payload container `11965ba5c8e899de75663ed6f619772a149863603c2a10d60ebb74de03e8d7b3`
- expanded payload `06b1edaeb66aea4de440a118872d7e3b689b889df0deaa3c560b0ebb75d8dcfb`
- UEFI FD `869ef9b54566265be2bd3a50158759798c95b392031676bc49e3f2f50ca58a2e`

已一次 `fastboot boot` 发送（`Booting OKAY`），USB 再次消失；屏幕/新持久日志尚待
确认。该轮的目标是越过 `could not set up inheritance port`，不能把传输成功写成
launchd 或系统成功。


## 4.3 第二个具体用户空间错误：launchd_cache_loader stashed ports

MIG register 兼容镜像 `4b90271f…` 的实际启动日志
`out/p21/persistent-logs/20261027T025903Z`（boot_id `0x34528fd2`，28 次提交，
write_errors=0）显示：

- launchd 越过 `could not set up inheritance port`；
- `launchd_cache_loader` 启动后执行 boot task；
- 随后 `pid 22 exited -- exit reason namespace 7 subcode 0x1, description
  Unable to get stashed ports: 268451844`。

`268451844` = `0x10004004` = `MACH_RCV_TOO_LARGE`。这与旧
`mach_ports_lookup` 回复布局不匹配完全吻合：当前 XNU10063 服务器回复 3 个 inline
port descriptor，而 iOS16/XNU8796 的 `mach_ports_lookup` stub 只准备了
OOL-ports-array 回复缓冲区，收到更大的新版回复即报 `MACH_RCV_TOO_LARGE`。

上游差异：

- XNU8796 `mach_ports_lookup` 回复：`descriptor_count=1`，
  `MACH_MSG_OOL_PORTS_DESCRIPTOR`，NDR，`init_port_setCnt`。
- 当前 XNU10063 `_kernelrpc_mach_ports_lookup3` 回复：3 个
  `MACH_MSG_PORT_DESCRIPTOR`，`descriptor_count=3`。
- iOS16 launchd 本身只 import `_mach_ports_register`，不 import
  `mach_ports_lookup`；失败发生在它启动的 `launchd_cache_loader` 子进程。

本轮修复：

1. 在生成的 lookup 服务器中增加旧回复布局：分配 3 个 `mach_port_ool_t` 数组，
   填入 `_kernelrpc_mach_ports_lookup3()` 返回的三个端口，按旧 MIG 布局发送
   OOL-ports-array 回复（`descriptor_count=1`，size 0x40，disposition 19）。
2. 用 `ios16_legacy_mach_ports` 选择协议：仅当 iOS16 根
   `E652776C…` 的 owner boot manifest 路径成功执行时置 1；其他根仍走当前
   3-port 回复，保留新版行为。
3. 旧回复的端口权限仍由 `_kernelrpc_mach_ports_lookup3()` 产生，发送用
   MOVE_SEND；没有伪造 `KERN_SUCCESS` 或跳过端口安装。
4. 补丁保存为 `scripts/p23-patch-task-server-lookup.py`，并由
   `scripts/p23-mig-wrapper.sh` 在 MIG 生成后与 register 补丁一起应用。

最终镜像：

- `uefi/boot-p23-ios16-8cpu-1g.img`，75,407,360 字节
  SHA256 `f0af5e2da1dd4f1146bc1ca76045649c170a7898d3b89c927704db4c5d39d447`
- `kernel/kernel.macho`，117,796,864 字节
  SHA256 `daa5b742d5322d02d75b0e695b207df222868bd69b57d645756c8cf34e0077cf`
- payload container `11965ba5c8e899de75663ed6f619772a149863603c2a10d60ebb74de03e8d7b3`
- expanded payload `06b1edaeb66aea4de440a118872d7e3b689b889df0deaa3c560b0ebb75d8dcfb`
- UEFI FD `869ef9b54566265be2bd3a50158759798c95b392031676bc49e3f2f50ca58a2e`

已发送一次 `fastboot boot`，返回 `Booting OKAY`；按用户要求发送成功后停止并等待，
不再自动重启或循环。屏幕/新日志待回读。


## 4.4 `ipc_right_copyout: strange rights`

旧 lookup 回复第一次实现使用 disposition 19（COPY_SEND）。旧 iOS16 MIG 的
lookup 回复检查要求 disposition **17（MOVE_SEND）**；当前 XNU10063 的
`ipc_right_copyout()` 只处理 RECEIVE/SEND/SEND_ONCE，收到 COPY_SEND 走到
`default: panic("ipc_right_copyout: strange rights")`。

修复：旧 OOL-ports-array 回复的 descriptor disposition 改为 17；布局、size 0x40、
count 和 OOL 数组保持旧协议。这样旧 MIG 检查通过，且 copyout 走
`MACH_MSG_TYPE_PORT_SEND` 的正常路径。

最终镜像：

- `uefi/boot-p23-ios16-8cpu-1g.img`，75,403,264 字节
  SHA256 `407eb481020fe3ff3a1e837bb72630dc41e9921de30dd5d59cbe2a71682aac8c`
- `kernel/kernel.macho`，117,796,864 字节
  SHA256 `daa5b742d5322d02d75b0e695b207df222868bd69b57d645756c8cf34e0077cf`
- payload container `11965ba5c8e899de75663ed6f619772a149863603c2a10d60ebb74de03e8d7b3`
- expanded payload `06b1edaeb66aea4de440a118872d7e3b689b889df0deaa3c560b0ebb75d8dcfb`
- UEFI FD `869ef9b54566265be2bd3a50158759798c95b392031676bc49e3f2f50ca58a2e`

已发送一次 `fastboot boot`，返回 `Booting OKAY`；按用户要求发送成功后停止等待。

## 5. 下一步（需要设备回到可读状态）

1. 人工观察屏幕并记录可见文字/panic；若黑屏，按住电源+音量进入 Recovery。
2. 设备回到 Recovery 后先运行：
   `scripts/logging/read-cheeseburger-log.py`，读取 P23 轮次的持久日志。
3. 根据日志中的 CPU online、DISPLAY stage/clients/frames、SWAP、TOUCH、
   text-fixup、first-fatal 判断实际到达的内核/用户空间阶段。
4. 若日志仍停在早期内核，再定位具体 ABI/驱动错误；不要自动循环重启。
5. 回滚只需从 `backup/userdata-before-8948MiB.img` 恢复 8948 MiB；
   历史 R7/P12 产物和日志均保留。
