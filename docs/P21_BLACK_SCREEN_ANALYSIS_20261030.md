# P21 黑屏分支日志分析报告（2026-10-30）

目的：把“同一张镜像有时进系统、有时黑屏”的现象整理成可复核的证据链，供另一位 agent 独立核对。
所有结论都标注 **已确认 / 推断 / 未验证**；原始数据都在 `out/p21/persistent-logs/<UTC>/`，未提交到 Git。

## 1. 分析范围（8 个近期 run + 2 个历史对照 run）

| # | 候选目录 | 镜像 SHA-256（前 8 位） | 镜像内内核 pin | ADT 身份 | 日志目录 | boot ID | 用户态结果 |
|---|---|---|---|---|---|---|---|
| 1 | `out/p21/upstream-a` | `7cf2fef9` | `cf6a9bfd` | iPad7,1 | `20261029T153842Z` | `0x59359b02` | 进入系统（`frames=18`） |
| 2 | `out/p21/up1-clean-rebuild` | `7aabe63b` | `cf6a9bfd` | iPad7,1 | `20261029T161111Z` | `0x55da2956` | 进入系统（`frames=27`） |
| 3 | `out/p21/touch-window2g` | `29029186` | `cf6a9bfd` | iPad7,1 | `20261029T164350Z` | `0x5c58198d` | 进入系统（`frames=39`） |
| 4 | `out/p21/phone-ui` | `53b8f0df` | `cf6a9bfd` | iPhone10,3 | `20261029T165823Z` | `0x4dd45577` | 黑屏（`frames=0`） |
| 5 | `out/p21/phone-ui-d2` | `1999371c` | `fe6be784` | iPhone10,3 + `/buttons` | `20261029T172431Z` | `0x5a0ea281` | 进入系统（`frames=14`） |
| 6 | `out/p21/touch-drive` | `db1385a3` | `bbbb2099` | iPhone10,3 + `/buttons` | `20261030T040140Z` | `0x4f5bb00c` | 黑屏（`frames=0`） |
| 7 | `out/p21/touch-fix2` | `80c8b19e` | `031cf674` | iPhone10,3 + `/buttons` | `20261030T044653Z` | `0x4bfec417` | 黑屏（`frames=0`） |
| 8 | `out/p21/phone-ui-d2`（复跑） | `1999371c` | `fe6be784` | iPhone10,3 + `/buttons` | `20261030T050326Z` | `0x50e2b156` | 黑屏（`frames=0`） |
| 9 | `out/p21/r17`（时间对应，boot-once 日志缺失） | `57a505ea` | — | iPad7,1 | `20261026T054456Z` | `0x3b368de7` | 黑屏（`frames=0`） |
| 10 | 同上 | `57a505ea` | — | iPad7,1 | `20261026T061553Z` | `0x392053dd` | 黑屏（`frames=0`） |

说明：#9/#10 是触摸、身份、2 GiB 窗口这些改动**之前**的历史 run，用来证明失败签名不是新引入的（镜像对应依据启动时间，属**推断**）。
#5 与 #8 是**同一张镜像**，结果不同，这是本报告的核心对照。

## 2. 每个 run 的关键计数器（已确认，取自 `decoded/*/latest.txt`）

| 日志 | DISPLAY | SWAP | 进程退出 | TOUCH |
|---|---|---|---|---|
| `20261029T153842Z` | `clients=24 frames=18` | `check=18 transfer=18 rc=0` | 820 | — |
| `20261029T161111Z` | `clients=10 frames=27` | `check=27 transfer=27 rc=0` | 1277 | — |
| `20261029T164350Z` | `clients=11 frames=39` | `check=40 transfer=39 rc=0` | 490 | `error=-54` |
| `20261029T165823Z` | `clients=5 frames=0` | `check=0 rc=ffffffffffffffff` | 481 | `error=-54` |
| `20261029T172431Z` | `clients=115 frames=14` | `check=14 transfer=14 rc=0` | 1088 | `error=-54` |
| `20261030T040140Z` | `clients=5 frames=0` | `check=0 rc=ffffffffffffffff` | 618 | `error=-58` |
| `20261030T044653Z` | `clients=5 frames=0` | `check=0 rc=ffffffffffffffff` | 678 | `error=-30 i2c=0/1` |
| `20261030T050326Z` | `clients=5 frames=0` | `check=0 rc=ffffffffffffffff` | 599 | `error=-54` |
| `20261026T054456Z` | `clients=4 frames=0` | `check=0 rc=ffffffffffffffff` | 357 | — |
| `20261026T061553Z` | `clients=5 frames=0` | `check=0 rc=ffffffffffffffff` | 1122 | — |

所有 run 的 `DISPLAY stage=10 meta=0 reg=1` 都相同：`native_legacy_stage` 走到 10，`meta` 状态为 0，注册表已见。

## 3. 失败/成功在客户端行为上的分界（已确认）

`latest.txt` 里 `[display-clients-live]` 的 `FB*` 行（`in/out` = 被拦截到的方法调用进入/退出次数，`enter/leave` = 最后一次进入/退出的 **selector 编号**，`rc` = 该调用的返回）：

| 结果 | backboardd 行 | 其他客户端行 | `[scanout prepare]` |
|---|---|---|---|
| 成功（#1） | `in/out=97/97 enter/leave=5/5 rc=0` | MobileGestaltH `rc=e00002f0`；PosterBoard `rc=0` | `calls>0` |
| 成功（#2） | `in/out=137/136 enter/leave=5/4 rc=0` | FDR_Load `8/8 rc=0` | `calls>0` |
| 成功（#3） | `in/out=194/194 enter/leave=5/5 rc=0` | FDR_Load `28/28 rc=e00002f0`；CollectionsPos `rc=0` | `calls>0` |
| 成功（#5） | `in/out=77/77 enter/leave=5/5 rc=0` | bics_dae `8/8 rc=0`；FDR_Load `0/0` | `calls=14` |
| 黑屏（#4） | `in/out=15/15 enter/leave=50/50 rc=e00002c7` | MobileGestaltH `28/28 rc=e00002f0`；FDR_Load `0/0` | `calls=0` |
| 黑屏（#6） | `in/out=15/15 enter/leave=50/50 rc=e00002c7` | MobileGestaltH `rc=e00002f0`；FDR_Load `0/0` | `calls=0` |
| 黑屏（#7） | `in/out=0/0 rc=0` | **bics_dae** `15/15 enter/leave=50/50 rc=e00002c7`；MobileGestaltH `rc=e00002f0` | `calls=0` |
| 黑屏（#8） | `in/out=0/0 rc=0` | **FDR_Load** `15/15 enter/leave=50/50 rc=e00002c7`；MobileGestaltH `rc=e00002f0` | `calls=0` |
| 黑屏（#9/#10） | `in/out=15/15 enter/leave=50/50 rc=e00002c7` | MobileGestaltH `rc=e00002f0` | `calls=0` |

要点：

1. **所有黑屏 run 都有 `kIOReturnNotOpen`（`0xe00002c7`）**，且最后一次外部方法调用的 selector 是 **50**；所有成功 run 的 backboardd 都走到了 **selector 5（swap_submit）并返回 0**。
2. 黑屏 run 的 backboardd 要么一次方法都没发出（`in/out=0/0`，由 bics_dae / FDR_Load 代它去试），要么试到 selector 50 就被 `NotOpen` 拒绝；`prepare calls=0` 说明连我们自己实现的 scanout 准备都没被调用过。
3. 这条签名在 **2026-10-26 的 R17 run** 里已经存在（#9/#10），与触摸、手机身份、2 GiB 窗口、`kIOMapWriteCombineCache` 都无关。

## 4. 内核侧已确认没有差异

- `IOMobileGraphicsFamily` kext 启动：成功 run 74.0 s（`172431Z`）、黑屏 run 65.9 s（`050326Z`），先后顺序与内容一致（`IOMobileFramebufferUserClient` → `IOMobileFramebuffer` → `IOMobileFramebufferService` → `IOMobileFramebufferLegacy` → `NativeLegacyDisplay` → `NativeGOPPresenter`）。
- `NativeLegacyDisplay` 的 `DISPLAY stage=10 meta=0 reg=1` 两者相同；`PMRD: joinAggressiveness NativeLegacyDisplay` 都出现。
- APFS 数据卷两边都挂载（成功 run `disk0`，复跑 run `disk1`，块数同为 806400；命名差异只是设备编号顺序）。
- payload 逐文件对比（#5 vs #7）：`unixnu.toml`、ADT、trustcache、Data seed 全部相同，唯一差异是内核；内核源码差异只有 `CheeseburgerTouch.cpp`；`__text` 尺寸差 0x30。**已确认**触摸/身份不是黑屏变量。

## 5. 时间维度（已确认的观察，未解释）

- 成功 run 的**首帧时刻**：`164350Z` t≈307 s，`172431Z` t≈318 s，`161111Z` t≈524 s。
- 黑屏 run 观测到 t=881 s（`050326Z`）与 t=977 s（`040140Z`）仍为 0 帧，`prepare calls=0`。
- 因此“还没等到”不能解释黑屏 run，两者是不同分支。

## 6. 候选原因（按证据强度）

### A. framebuffer 用户客户端 open 的连接时序竞态（推断，证据最强）
失败分支里客户端拿到 `kIOReturnNotOpen` 后不再成功连接；成功分支里 backboardd 正常拿到 selector 5。
内核侧发布时序、kext 加载、payload 都相同，所以变量落在“客户端发起请求时服务是否已可 open”。
**可证伪**：如果失败 run 里 selector 50 的调用发生在 `native_legacy_stage` 已经到 10 之后很久（服务早已发布），则说明不是“发布太晚”，而是 open 路径本身返回 NotOpen。

### B. 用户态启动节奏 / 数据卷状态（推断，与 A 可能叠加）
失败 run 的进程退出数只有成功 run 的 55%–62%，SpringBoard 停在 8 线程（成功 17），msgbuf 里 datamigrator、语言服务、bluetoothd 反复重启。可能让“就绪窗口”与客户端重试策略错开。
**可证伪**：在同一份数据卷上重复启动同一镜像多次，统计结果分布；若失败与 datamigrator/bluetoothd 阶段强相关，则成立。

### C. 内核显示路径 / 像素通路（已排除）
`prepare calls=0`、`SWAP rc=ffffffffffffffff` 表示从未提交，问题不在拷贝或 scanout 实现；而且历史 R17 就是这样。

### D. 触摸改动 / 手机身份 / 缓存映射（已排除）
见第 4 节：同镜像两种结果、payload 相同、历史 run 已有相同签名。

## 7. 上游核对（已确认的差异清单）

对照官方 `https://github.com/UniXNU/UniXNU`（本地 `upstream/UniXNU`，`docs/reference/troubleshooting.md`）与官方 sheng（本地 `upstream/UniXNU-sheng`，同为 iPadOS 17.7.11 / 21H461、iPad7,1）：

1. 官方 troubleshooting 有一行：“**Firmware stalls → Random Silicium stall → Start the same image again**”，即上游明确承认会随机卡住、建议重跑同一镜像。与我们对 #5/#8 的观察一致，但**没有**说明用户态黑屏是否同一类问题。
2. 官方 sheng `board.toml` 的 `kernel_args` 与本机 P21 参数差异：

| 项 | sheng（iPad7,1, 21H461） | 本机 P21 | 备注 |
|---|---|---|---|
| `offline-legacy-unit-scale` | `2` | `1` | 只影响缩放语义 |
| `offline-mobile-graphics-client` | `1` | 未设置 | 上游的额外客户端探针 |
| `sheng-disable-jobs` | `identityservicesd,locationd,CommCenter,bluetoothd` | 未设置 | 失败 run 里 bluetoothd 反复重启 |
| `boot-ui-focus3/4` | `Preferences` / `SpringBoard` | 未设置 | 经查只驱动诊断埋点（`package/BootProcessProbe.c`），不影响功能 |
| `debug` | `0x14e` | `0x8` | 调试级别 |
| 其他 | `kt_fixed_zones=120`、`-apfs_shared_datavolume`、`serial=3` | 无 | 内存/挂载项 |

3. 上游 `package/LegacyDisplayProbe.cpp` 的 attach 流程与本机不同（本机在其基础上加了自己的 attach 策略和大量诊断），**没有可直接 cherry-pick 的修复**。
4. 上游没有 “NotOpen 黑屏” 的文档或 issue；`NativeGOPPresenter.cpp` 里 `kIOReturnNotOpen` 是“客户端未 open”的既有语义，不是错误日志。

## 8. 建议的下一步诊断（尚未执行）

1. 在 `package/BoardRuntime/LegacyDisplayProbe.cpp` 的 `wrappedExternal()` 增加一次性记录：selector 50 与 open/close 的 pid、名称、时间、rc、当时的 `native_legacy_stage`、`seenClientCount`。
2. 用同一镜像连续跑 2–3 次，直到出现一次成功和一次失败，比较 selector 50 的相对时序。
3. 若确认是“服务未就绪”，评估两种改法：
   - 提前/确定性地发布 provider；
   - 让 open 路径返回可重试错误（需先证明原始客户端会重试，不能盲改语义）。
4. 并行对照：按上游加 `sheng-disable-jobs=com.apple.identityservicesd,com.apple.locationd,com.apple.CommCenter,com.apple.bluetoothd`，观察黑屏概率是否下降（缓解手段，不是根因结论）。

## 9. 观测限制

- 持久日志是 userdata 相对 10 GiB、32 MiB 的环形窗口；`msg_blocks=256`，长 run 的早期内核消息可能被覆盖。
- 没有 21H461 dyld shared cache 在本地，`ui-thread-stacks.json` 里的用户态 PC 未符号化（历史 run 的 `symbolicate-threads.py` 需要 `/Volumes/DawnUpdate21H461...`，当前未挂载）。
- `FB*` 行是抽样快照（`p21_client_rows` 注明“approximate live counts”），`enter/leave` 是 selector 编号而非调用次数。
- 未做 Data 清理、未改任何分区；以上 run 全部是临时 `fastboot boot`。

## 10. 复现与取数命令

```sh
# 回收日志（设备在 root Recovery）
CHEESEBURGER_ADB_SERVER=tcp:localhost:5037 \
  /opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py

# 关键字段
grep -a "DISPLAY stage\|SWAP check\|EXITS total\|TOUCH stage" \
  out/p21/persistent-logs/<UTC>/decoded/<tag>/latest.txt
grep -a "^FB" out/p21/persistent-logs/<UTC>/decoded/<tag>/latest.txt
```
