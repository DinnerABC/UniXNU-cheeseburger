# P17 续接

当前候选为P17-R4：`out/p17/r4/uefi/boot-p17-r4-official-scratch.img`，81,883,136字节，SHA256 `182b5e1fa6cf4220b2cbbb7ad7e4e0d767160c7a1c0e37d80162f92dd5f35df8`。

内核严格复用R3，SHA256 `e88abc279ba751f4c400e930f429eca592b32138961475924e66f209336902e1`，没有重编XNU、原始驱动或安全组件。离线门禁`P17_OFFLINE_BOOT_GATE_PASS`。当前实机记录`HARDWARE_DATA_WRITABLE_TZINIT_SYMLINK_ZERO_HUMAN_CONFIRMED`，本候选boot命令数1，分区写入0。**同一内核的屏幕标题仍会显示P17-R3；候选身份以本次实际发送SHA和独立启动记录为准。**

先读out/p17/r4/boot-result.json（如已创建）、offline-gate.json、seed-packet-check.json、data-seed/verification.json与SHA256SUMS。R2/R3人工核准证据分别在各自evidence目录，勿再用旧OCR转录替代。

当前唯一内核源树不变；R4不编XNU。原镜像、seed、协议、固定XNU/Apple组件、Root Owner Trust、SystemOS、Boot Manifest保持，userdata/GPT/OS/SystemOS不写。Data RAM-only，不启用尾部store。

R4只回归官方未指定角色的APFS scratch语义；原始APFS加密检查仍原样存在。若未来需要正式角色Data，必须实现真实iOS per-file encryption、keybags和对应设备安全能力，不能把宿主口令加密当成它，也不能清UNENCRYPTED标记造假。

AGENTS检查在out/p17/r4/baseline/agents-scope-check.json及data-seed/agents-scan.json；项目、父目录和可见代理规范中没有找到适用AGENTS.md，其他插件局部规范未套用。

本候选一次临时boot，失败或失联不重发；不能删除state绕门。启动后保留首fatal，新照片先转录、用户人工核准后写入。先看owner/SystemOS/Manifest，再看Data stage/readonly/writes、tz calls/errno/exit-request及后续首错。若成功越过tzinit，继续真实Sandbox、Keybag/Data Protection、FIPS和系统服务，不人为按阶段停止，不关闭安全组件，不开展外围图形驱动。

流水线在out/p17/r4/scripts：seed角色修改和重挂验证完成→seed-role-finalize→prepare-payload.sh→finish-firmware.sh。已过内核检查只在R4核对冻结报告及相同hash，不重复编译。VM仅更新变化的CompressedPayload头和实际FV，kernel/ADT Expected头内容不变。日志标签不可复用，失败续接用新标签并同步finalgate，不盲目重跑整条流水线。

用户恢复Recovery的授权已给；每次仍需固定序列号、cheeseburger身份、Bootloader产品/unlocked/max-download-size、候选SHA全部通过。R4关键状态现已人工核准；后续新照片仍先转录再由用户核准，不能把未核准地址补为确定值。

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

后续子代理约定：用户允许最多三名轻量快速子代理；照片转录、定向源码审查与日志核验可分工，关键数字由主代理复核并经用户人工确认。旧r2_pipeline/r2_ram_path已停用。本轮不为凑满数量启动额外工作。
