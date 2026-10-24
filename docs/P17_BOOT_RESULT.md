# P17 实机结果

当前候选为P17-R4：`out/p17/r4/uefi/boot-p17-r4-official-scratch.img`，81,883,136字节，SHA256 `182b5e1fa6cf4220b2cbbb7ad7e4e0d767160c7a1c0e37d80162f92dd5f35df8`。

内核严格复用R3，SHA256 `e88abc279ba751f4c400e930f429eca592b32138961475924e66f209336902e1`，没有重编XNU、原始驱动或安全组件。离线门禁`P17_OFFLINE_BOOT_GATE_PASS`。当前实机记录`HARDWARE_DATA_WRITABLE_TZINIT_SYMLINK_ZERO_HUMAN_CONFIRMED`，本候选boot命令数1，分区写入0。**同一内核的屏幕标题仍会显示P17-R3；候选身份以本次实际发送SHA和独立启动记录为准。**

## 已人工核准的结果

| 项目 | R2 | R3 |
| --- | --- | --- |
| 首fatal | Failed to mount root device，IOKitBSDInit.cpp:985 | unencrypted data volume is not allowed，apfs_vfsops.c:2276 |
| owner | 0 | 1 |
| SystemOS status/auth | -1/0 | 0/1 |
| Manifest | -1 | 0 |
| RAM reads/writes/failures | 41/0/0 | 75/0/0 |
| Data stage/status/readonly | 0/-1/-1 | 2/-1/-1 |
| activation status/elapsed | 不适用 | 0/2045ms |
| Data设备路径 | 空 | /dev/disk11s1 |
| tz calls/errno/exit-request | 0/-1/-1 | 0/-1/-1 |
| PID1 EL0 traps | 0 | 0 |

R2 S1=10001ms/n0/O0/S0/D0，S2=30002ms/n0/O0/S0/D0，S3=55003ms/n79/O0/S1/D1。M0 Data disk1s1/1073741824字节，M1 SystemOS disk9s1/3303014400字节。完整人工核准清单与照片在out/p17/r2/evidence。

R2的n只计fully matched IOMedia，不等于原始注册树为空。资源none仅指第一个匹配IOResources上的OSString未观测，不能推出ADT缺UUID。实际P12 UFS先等SystemOS成功才发OS，失败discard未启动OS，所以OS缺失符合前置失败链。新增RAM早期初始化为时序干扰候选，未直接量测SHA子阶段，不能把1GiB SHA耗时写成唯一实证根因。

R3照片与两段核准原文在out/p17/r3/evidence。stage2表示真实mount内部，status=-1仍为初始值，不是mount返回errno=-1。R3恢复了根认证链并进入新增Data挂载，但没有超过P16已达到的用户空间深度。

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
