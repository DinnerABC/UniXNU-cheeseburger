#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,datetime,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p8'
pack=json.loads((o/'uefi/packaging.json').read_text());kernel=json.loads((o/'kernel/validation.json').read_text());adt=json.loads((o/'adt/validation.json').read_text());assert pack['result']=='PASS' and pack['compiled_darwinboot_hash_gates_match']
files={}
for name in ['kernel/kernel.macho','adt/kernel.macho.adt','payload/payload.bundle','uefi/MSM8998PKG_UEFI.fd','uefi/boot-p8-xnu.img','native-ufs/libNativeUfs.a','storage-family/libIOStorageFamily.a','platform/libArmCpu.a','ram-block/libNativeRamBlock.a']:
 p=o/name;files[str(p.relative_to(r))]=dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
assert files['out/p8/kernel/kernel.macho']['sha256']==kernel['sha256'];assert files['out/p8/adt/kernel.macho.adt']['sha256']==adt['sha256'];assert files['out/p8/uefi/boot-p8-xnu.img']['sha256']==pack['sha256']
required=['build-storage-family-03','build-native-ufs-02','build-platform-02','build-ramblock-01','link-kernel-03','test-ufs-policy-01','test-interrupt-contract-01','build-uefi-06','export-uefi-05','verify-native-images-05','pack-bootimg-05']
for label in required:assert json.loads((o/'logs'/(label+'.json')).read_text())['exit_code']==0,label
subprocess.run(['python3',str(r/'tools/p8/device_loop.py'),'verify',str(o/'uefi/boot-p8-xnu.img'),'--sha256',pack['sha256']],check=True)
summary=dict(result='BUILD_AND_OFFLINE_CHECKS_PASS',files=files,passing_logs=required,latest_candidate_hardware_tested=False,tested_run='20261022T104601Z-87a9ed62',tested_image_sha256='42ea23280d01cd87e42ffe2b699f8a3c61b3e32accd95d2d9ae08bd4eba602c8',deepest_hardware_evidence='User OCR: continuously printed IOKit IODeviceTreeSupport.cpp unexpected warning',ufs_reads_hardware_proven=False,root_image_created=False,launchd_proven=False,safe_to_flash=False)
(o/'final-manifest.json').write_text(json.dumps(summary,indent=2)+'\n')
text=f'''# P8 交接

本轮已真实进入 IOKit 设备树处理；其余结论和证据边界见 [实机结果](P8_BOOT_RESULT.md)。手机在唯一一次测试后 USB 离线，自动控制器已停止。没有刷写、擦除、解锁或修改关键分区。

## 当前正式候选（修正中断属性契约后）

| 文件 | 字节 | SHA256 |
|---|---:|---|
'''
for name in ['kernel/kernel.macho','adt/kernel.macho.adt','uefi/MSM8998PKG_UEFI.fd','uefi/boot-p8-xnu.img']:
 v=files['out/p8/'+name];text+=f"| `out/p8/{name}` | {v['bytes']} | `{v['sha256']}` |\n"
text+='''
这些文件编译及离线回读通过，**尚未实机验证**。真实通过的 build/verify 标签和归档哈希在 `out/p8/final-manifest.json`。实际 DarwinBoot PE 内嵌的 kernel/ADT SHA 与当前 payload 一致，不能用旧固件搭配新 payload。

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

详细实现见 [P8_IMPLEMENTATION.md](P8_IMPLEMENTATION.md)，真实测试见 [P8_BOOT_RESULT.md](P8_BOOT_RESULT.md)。本阶段结束时没有自动实机重试；跨重启电子日志回收、UFS、APFS、BSD、launchd、SpringBoard 均不能标记通过。
'''
(r/'docs/P8_HANDOFF.md').write_text(text)
p=r/'README.md';s=p.read_text();lines=s.splitlines();idx=next(i for i,x in enumerate(lines) if x.startswith('截至 **2026-10-22**'))
lines[idx]='截至 **2026-10-22**，P7 实机已通过用户观察确认 MMU、高地址代码、C 入口、VM 与 PE 路径。P8 已真实编译并链接只读存储模块，单次临时启动出现 IOKit 设备树处理日志；针对重复 warning 的修正版已构建但尚未上机。**UFS 读取、根卷挂载和 iOS 用户空间仍未证实**。当前状态及下一步见 [P8 交接](docs/P8_HANDOFF.md)、[实现](docs/P8_IMPLEMENTATION.md)和[实机结果](docs/P8_BOOT_RESULT.md)。'
p.write_text('\n'.join(lines)+'\n')
print(json.dumps(dict(final_boot_sha256=pack['sha256'],kernel_sha256=kernel['sha256'],adt_sha256=adt['sha256'],hardware_tested=False),indent=2))
