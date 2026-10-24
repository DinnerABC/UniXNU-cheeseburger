# P8 交接

本轮已真实进入 IOKit 设备树处理；其余结论和证据边界见 [实机结果](P8_BOOT_RESULT.md)。手机在唯一一次测试后 USB 离线，自动控制器已停止。没有刷写、擦除、解锁或修改关键分区。

## 当前正式候选（修正中断属性契约后）

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `out/p8/kernel/kernel.macho` | 19459152 | `be7f8bcca1de3e6f44dc59298899ea0c590a648f9af7dda75eeba2cf894979d7` |
| `out/p8/adt/kernel.macho.adt` | 14600 | `883aa5a8a554c687fe6ffe38c4e6ef683f595c424ac912f746c7f80b2b6b74e0` |
| `out/p8/uefi/MSM8998PKG_UEFI.fd` | 6291456 | `0f02505e10ee4706a1113f9de4d56ae5670380ff40b99be7d53b67d0b208fbfb` |
| `out/p8/uefi/boot-p8-xnu.img` | 25604096 | `a4458921798976dba7e57f220f78163b488a18cb9d5da5e615c36770986629d2` |

这些文件编译及离线回读通过，**现已在后续授权测试中临时启动，但未验证启动成功**，结果见文末。真实通过的 build/verify 标签和归档哈希在 `out/p8/final-manifest.json`。实际 DarwinBoot PE 内嵌的 kernel/ADT SHA 与当前 payload 一致，不能用旧固件搭配新 payload。

已测试的旧版本冻结在 `out/p8/tested/20261022T104601Z-87a9ed62/`。其镜像 SHA 是 `42ea23280d01cd87e42ffe2b699f8a3c61b3e32accd95d2d9ae08bd4eba602c8`，不是当前修正版。旧版本不得重复测试。

## 首个具体故障及修复

用户屏幕持续输出 `unexpected .../IODeviceTreeSupport.cpp:6...`。实际 IOKit 源码和旧 ADT 可复现第 610 行循环：通用 `interrupts` 的父节点无完整 interrupt-cells/controller/map，映射器不推进。

修复只调整驱动与 ADT 的契约。ArmCpu 使用 `p8,timer-intid=19`，保留真实 IOCPUInterruptController、GIC dispatch、rtclock；轮询式 UFS 保存 `p8,spi-intid=297`，不广告一个不存在的通用 SPI 服务。没有改动核心映射器、屏蔽断言或伪造驱动成功。回归见 `out/p8/interrupt-contract/result.json`，精确手机 PC 未采集，修复上机效果仍待验证。

## 继续执行顺序

1. 先保留手机现有屏幕证据，再由人工长按电源恢复 Android；只读确认身份在线后才可进行新的临时启动。当前没有授权假定它已恢复，未再发手机命令。
2. 对上述具体修复进行一次针对性集成测试，使用当前正式候选和 `config/p8/boot-profile.json`。复用 `tools/p8/device_loop.py`，保留身份/备份/DTB/hash 门禁和失联停止策略，不新增色条阶段。
3. 若通过 IOKit DT 分配，读取真实 ArmDTPlatform/ArmCpu/计时器输出，继续确认 NativeUfs 的 warm state、NOP、REPORT LUNS、容量、只读 READ 和 IOMedia。未获得这些证据前不能宣称存储可用。
4. 只有存在真实冷链路阻塞时，依据固定一加源码补齐时钟/PHY/电源依赖；不能拷贝 sheng SMMU、时钟或固件队列。
5. Root 还缺匹配的 APFS/AMFI/可信输入及 SHNGROOT window 子 IOMedia 层。iPhone XR 17.6 原始 kernelcache 是 arm64e / xnu-10063.142.1，与当前普通 ARM64 / 10063.141.1 不同；没有混入它的偏移、驱动、身份或 Trust Cache。下一步须解决实际 ABI/驱动适配，不能仅因 Darwin 同为 23.6 就预链接。

## 环境和复现

- 主入口 `scripts/p8-build.sh`，遇错停止；不包含手机操作。所有新产物在项目内。
- 唯一 XNU 工作树 `.work/p4-unixnu/.cache/xnu-work`；`-j2`，内核编译前停止 Linux VM。
- P8 UEFI 为 `/home/dinnerabc.guest/unixnu-p8/source` 独立 overlay，VM 磁盘在项目 `.l/`。每次 VM 重启必须执行 `p8-guest-overlay.sh` 重挂；主脚本已包含。
- 一次会话中断的 build-uefi-04 没有退出状态，已标注 interrupted。之后发现生成 `BuildEnv.sh` 含 NUL，原件及哈希保存在 `out/p8/interrupted-cache/`；仅该生成缓存被移除重建，build-uefi-06 真实退出 0。
- 一份从未上机的旧固件误混包已放入 `out/p8/invalid/stale-uefi-export/`，禁止使用。当前 packer 额外核对实际 loader 二进制内嵌哈希。
- P7 kernel/ADT/boot 和 P3 FDT 哈希由 `p8-validate-inputs.py` 检查；历史证据保留。
- `NativeRamBlock` 真实 archive 已编译，但未提供 APFS Data seed、未激活。没有 `userdata-root.img`；geometry/阻塞原因在 `out/p8/root/`。
- 手机备份仍在私有 `backups/p3/`，不提交或公开。代码签名、Crypto POST 和真实性检查没有被禁用。

详细实现见 [P8_IMPLEMENTATION.md](P8_IMPLEMENTATION.md)，真实测试见 [P8_BOOT_RESULT.md](P8_BOOT_RESULT.md)。首轮交付时没有自动实机重试；跨重启电子日志回收、UFS、APFS、BSD、launchd、SpringBoard 均不能标记通过。


## 后续授权实测：2026-10-22 19:17–19:22（北京时间）

用户恢复 Android 后，已只读确认设备身份并测试上述正式候选，run `20261022T111743Z-a9a520d5`。用户观察刷屏后仅显示 `MACH Reboot`。当前源码将其定位到 `halt_all_cpus(TRUE)`，触发它的原始错误仍未采集；不能推断 UFS/BSD 成功。

观察超时，无 ADB/Fastboot 返回，控制器退出 2 并停止，没有自动复试。完整结果见 `P8_BOOT_RESULT.md` 末节及 `out/p8/latest-boot-result.json`。输入冻结在 `out/p8/tested/20261022T111743Z-a9a520d5/`。当前候选已尝试过，控制器会拒绝无意义重复同一 SHA。

优先方向改为：捕获并保留 panic/重启前日志，检查控制台回绕清屏，分辨错误触发与平台重启不返回。此轮没有修改内核或重新编译。手机未确认恢复前不再发设备命令；下一轮需人工恢复 Android。
