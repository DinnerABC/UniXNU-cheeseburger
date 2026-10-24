# P16 实机启动结果

**Owner Boot Manifest 已在实机正确发布并通过 IOGetBootManifestHash 读回；mount PID3 退出0，已越过 P15 的 mount 66。新的首个致命停止是 tzinit PID17 在只读文件系统上创建时区路径失败，随后正常退出30。**

用户第一张照片就是有效的 `P16 FIRST FATAL`，不需要为补拍而重复启动同一镜像。第二张为更早的控制台，不是最终fatal。本轮保持一次fastboot boot，未写分区、未重启或重发；当前照片足以定位新的失败。

## 产物保持

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| out/p16/kernel/kernel.macho | 124964864 | 59a5f3a18baa45244636a6ac8e042315d6e9e3d2f62a4eb1c4838f117e3ce649 |
| out/p16/uefi/boot-p16-owner-manifest.img | 81317888 | 3badf4a9b7ba3ce7d26c6f48a01cad71db9277cc7e2e92046f5208a41b27f0e5 |
| out/p16/payload/payload.container | 75186666 | 051b41c7aa9bc1ca2dad10e462f7d10980786c4fa3f507ca0f8cbb04f133d1be |

所有原始Apple可执行代码及19个组件与P15保持。完整离线门禁和11项publisher宿主测试已通过，构建输入与SHA见manifest/build-inputs/SHA256SUMS。P14/P15冻结产物不变。当前用户空间仍是iPad7,1 / 17.7.11 / 21H461参考，不改变最终iPhone iOS目标。

## 实机字段

原图、照片SHA、转录与证据边界保存至 `out/p16/evidence/`；`screen-result-01.json` 是本次运行结果，静态offline-gate保留其执行时状态。

| 字段 | 实机值 |
| --- | --- |
| owner accepted | 1 |
| SystemOS status / auth | 0 / 1 |
| Manifest status / host-match | 0 / 1 |
| 实际48字节SHA384 | f13cd57d18fad6f2786e2d2616edfa34e7b1aa5b6e30b707543fbb3b8122dac8f5d3ec347d497f4352bf2c44fcd8a308 |
| mount exit | PID3，status0 |
| PID1 exec stage | 4：PID1 exec调用返回成功 |
| 最近相关失败syscall | PID0x11=17，syscall0x39=57=symlink |
| errno / 路径 | 0x1e=30=EROFS，/var/db/timezone/zoneinfo |
| 原始panic | tzinit[17] exited，signal0，exit status30，无CS_KILLED标记 |
| Caller | 0xfffffff00bfbb010；按LR-4落在proc_prepareexit.cold.3+0x50 |

Manifest status0不是默认值：官方函数初值-1，只有实际IOGetBootManifestHash bool成功、长度48、全部字节比较一致后才保存actual并置0。屏幕actual与独立宿主预期一致，证明真实发布/读回路径运行。该摘要来自两份真实签名策略的Owner域，不是Apple固件个性化身份。

## 当前真正的错误

带进程前缀的tzinit输出依次为：

- Unable to locate data; resetting to system partition
- No such file or directory
- Last resort force_system_dataset() failed
- Read-only file system

同时记录到tzinit对 `/var/db/timezone/zoneinfo` 的symlink返回EROFS。随后critical-process退出触发内核panic，exit status30是十进制；不能误读为十六进制48，也不是签名kill或另一次exec ENOTSUP。

这直接说明本轮缺少可用Data路径，tzinit回退只读系统分区仍无法完成写入。已有候选没有启用RAM Data，和现场一致。不能靠修改已签名OS文件、屏蔽错误或重启同一镜像修复它。

## 更早的提示与其他未解决项

第二张照片记录SystemOS mount-complete、launchd尝试及两条 `vm_shared_region_start_address() failed`。固定vm_unix.c在shared-region检查失败时记录该提示并返回ENOMEM；此后实际已继续launchd任务和tzinit。因此它不是本轮最终停止原因，也不据此宣称全部shared cache映射正常。

第一张尾部还保留 `[16:wearlyboot fd=1] wearlyboot: Failed to enter sandbox`。output-token mask为0x3ff，意味着曾采到mount-phase-1、data-protection、mount-phase-2、init-with-data-volume、fips、all tests PASSED、result=true、keybag、FATAL KEYBAG ERROR、usermanagerd字样。**包括FATAL KEYBAG ERROR，不能宣布Keybag成功。**

mask仅是跨进程输出文字匹配，不是完成标志；FIPS的任务及测试成功字样被观察到，但早期来源/上下文已滚出尾部，不能独立认证FIPS整体结果。滚动丢弃0x8de字节，busy/truncated/copy错误均为0，仍不代表早期日志完整。没有为了补这些早期输出而重启原失败镜像。

## 九项交付结论

1. P15 mount66：P15确实缺少该输入；官方同故障stderr、P16正确发布后同一mount PID3退出0并推进至tzinit，构成修复有效的强因果证据。P15当时的内部首次失败调用仍未直接捕获。
2. Owner Manifest：使用真实OS/SystemOS签名策略、真实设备与UUID校验，按官方Owner域/角色SHA384生成。
3. 实机发布及读取：成功，status0/host-match1，完整48字节actual匹配。
4. mount-phase-1：mount PID3成功退出，已越过P15阻塞；phase2等任务字样已观察，不能仅凭字样证明各任务全部完成。
5. Data/FIPS/Keybag：Data当前不可用导致tzinit回退并EROFS；FIPS相关任务/测试字样已见；Keybag相关任务和FATAL错误字样已见，未成功。
6. LC_MAIN：原始用户空间及后续服务行为已执行，但LC_MAIN首指令完成仍无直接单步PC证据，继续保留边界。
7. 最深阶段：原始launchd后续任务已运行到PID17 tzinit，尾部还有xpcroleaccountd、init_featureflags、wearlyboot。
8. 新阻塞：可写Data及timezone路径；另保留wearlyboot Sandbox及Keybag错误待独立定位。
9. userdata：仍无需重新刷写且完全未写。下一步使用独立RAM scratch Data，不改GPT或只读OS/SystemOS。

现有32MiB Data seed虽然APFS结构/UUID有效，但raw inode审查显示宿主UID/GID污染，尚不能直接启用；NativeRamBlock静态190槽位/24覆盖/11导入已通过，真实集成及设备APFS写入仍需完成。
