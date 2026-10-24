# P17 可写 APFS scratch 实现

当前候选为P17-R4：`out/p17/r4/uefi/boot-p17-r4-official-scratch.img`，81,883,136字节，SHA256 `182b5e1fa6cf4220b2cbbb7ad7e4e0d767160c7a1c0e37d80162f92dd5f35df8`。

内核严格复用R3，SHA256 `e88abc279ba751f4c400e930f429eca592b32138961475924e66f209336902e1`，没有重编XNU、原始驱动或安全组件。离线门禁`P17_OFFLINE_BOOT_GATE_PASS`。当前实机记录`HARDWARE_DATA_WRITABLE_TZINIT_SYMLINK_ZERO_HUMAN_CONFIRMED`，本候选boot命令数1，分区写入0。**同一内核的屏幕标题仍会显示P17-R3；候选身份以本次实际发送SHA和独立启动记录为准。**

## 已完成与来源

P16已实机运行原始用户空间，但tzinit对/var/db/timezone/zoneinfo的symlink返回EROFS。P17继续使用官方NativeRamBlock、原始IOStorageFamily/APFS和native_owner_data_prepare，目标为开发用易失RAM scratch挂到/private/var。不是生产加密iOS Data，不宣称Keybag/Data Protection已实现。

旧32MiB seed的183个inode被宿主501:20污染，原文件和SHA保留于out/p16/data-seed。修复以认证原始OS只读ownership挂载为权威：管理员权限提取206个源路径、39个普通文件及32种UID/GID组合，逐项保留模式、flags、ACL、源xattrs、内容与软链；不递归统一chown。关键目录root/db/timezone为0:0，mobile为501:501/0711，run为0:1/0775，tmp为0:0/01777。缺失的keybags/容器目录留给真实系统服务建立。

每份新seed均实际owners-on重新挂载核验；宿主自动生成的com.apple.provenance与.fseventsd单独记来源，不冒充iOS源元数据。原始源xattrs必须完整保留。RAW真实APFS容器1GiB，DBRDISK1仅储存非零页，不把truncate冒充容器扩容。

NativeRamBlock绑定原始190槽位IOStorage ABI，采用晚期C++元类注册和独立匹配类别。11个原始storage别名只由冻结P12归档提供一份，store无provider保持RAM-only。R2已证实RAM Ready，但根挂载回退；R3将Data激活移至真实OS/SystemOS/Manifest成功之后，通过唯一UUID、BSD属性、真实provider与online seed后，进入未改的官方Data挂载函数。原UFS归档、原根30秒期限和安全匹配保持不变。

## R3人工确认与R4实际修复

R3人工确认owner1、SystemOS0/auth1、Manifest0，Data activation0/2045ms；Data stage2进入真实mount，原APFS在apfs_vfsops.c:2276报unencrypted data volume is not allowed，writes0，tzinit和EL0未到达。证据在out/p17/r3/evidence，冻结原始APFS指令报告在r3/data-driver/apfs-unencrypted-role-analysis.json。

真实原始guard读取role==0x40(Data)和fs_flags bit0(UNENCRYPTED)，两者同时成立进入原panic；原代码与donor逐字节一致。P17 seed制作额外changeVolumeRole D偏离了官方prepare-owner-data-fixture与QEMU scratch方案。R4只克隆旧seed，通过正式diskutil移除额外D角色，保持未加密标记为真，不patch APFS、不启用DoNotUseSEP、不伪造密钥或Keybag。

R4重新挂载核实角色None、相同真实卷UUID、真实1GiB容量及206源路径元数据；fsck、写入与timezone软链探针仅为host离线验证，不能替代手机证据。重新DBRDISK1打包与完整逻辑SHA比较；kernel/ADT/trustcache不变，只更新payload与固件身份。所有新结果out/p17/r4，旧seed和已测试镜像不覆盖。

## R4 人工核准实机结果（本轮结束）

用户核准关键状态并补全 dev=16777298。证据：out/p17/r4/evidence/screen-user-confirmed.json、同目录原照片及SHA256SUMS。

```text
owner=1 SystemOS=0 auth=1 Manifest=0
RAM ready=1 bytes=1073741824 resident=1609728
RAM reads=89 writes=5 failures=0
Data stage=4 status=0 readonly=0 dev=16777298
tz calls=1 errno=0 exit-request=0
PID1 EL0 traps=3879
Data path=/dev/disk11s1 act=0 ms=2045 t0=148651 t1=150696
```

真实APFS Data已可写挂载：stage4/status0/readonly0，设备/dev/disk11s1，dev16777298，RAM实际写入5次且failures0。原始tzinit被捕获的timezone软链调用返回0，已越过P16的EROFS阻塞；exit-request0仅为退出请求参数，尚无独立进程回收/退出完成证据。PID1已有3879次EL0调用，但不将其等同于launchd LC_MAIN首指令直接证明。

新的第一处fatal在照片中可读为ipc ports对象释放时zalloc元数据chunk长度检查失败（zalloc.c:1179）。这部分及底部launchd日志未在本次选择块中人工核准，地址不入正式数值记录；不能由检测点直接推断损坏来源。现有源码对应.work/p4-unixnu/.cache/xnu-work/osfmk/kern/zalloc.c的zone_invalid_element_panic，条件meta->zm_chunk_len > ZM_CHUNK_LEN_MAX。下一轮先取得准确panic与调用链，再分析ABI、对象释放和元数据损坏，不绕过检查。

Keybag、Sandbox、FIPS没有本轮新成功证明。可写RAM scratch不代表生产加密Data或Data Protection成功。userdata/GPT/OS/SystemOS未写，本候选只临时boot一次，不重发。本轮按用户要求仅落日志并停止后续修复、构建和设备操作。
