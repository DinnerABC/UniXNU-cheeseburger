# P21 · 10 GiB Data 与屏幕日志诊断（2026-11-01）

## 本轮运行

- 镜像 `d107ac79afa7a516a1f658eddeb69d8e57bb52f254b7aa72d9a4b8f9654106ae`，
  kernel `afb7d180…`，firmware `5cad6653…`，expanded payload `609c3007…`，args 635 B。
- 日志 `out/p21/persistent-logs/20261101T061144Z/`，boot `0x51875b27`，
  82 rounds，0 写入错误，uptime 408 s；心跳最后 `sec=408`。
- **返回方式是用户手动回 Recovery，不是 auto-reset**：`AUTORESET requested=600 elapsed=69 issued=0`，
  设备在 408 s 已回 Recovery。不能把本轮当作 400/600 s 自动返回的验证。

## 已确认的结果

### Data 扩容成功

`DATA persist state=3 header=1 pages=2621440 loaded=6291 writes=174814 syncs=29618 err=0 first=11261615 present=111415`

- `pages=2621440` 正好是 10 GiB / 4 KiB；`first=11261615` 与新布局一致。
- state=3（Fresh），header=1；本轮是新种子第一次启动。
- 控制台尾部 ENOSPC 计数为 0；上一轮 1 GiB Data 时的 `cloudd`/`db-shm` ENOSPC 已消失。
- 旧 1 GiB Data 没有迁移，符合 seed identity 变化后的预期。

### 触摸已恢复

`TOUCH stage=4 error=0 i2c=21408/0 publish=31 HID=31`
`TOUCH19 sink=31 send=31 rep=31 pend=1`

- 本轮触摸 worker 一直存活，HID sink 收到并发送 31 次报告；用户确认触摸可用。
- 上轮的 `stage=UINT64_MAX` / 三次 QUP 读失败退出没有复现；3 次失败即永久退出的代码缺陷仍在，需保留为 P2 的健壮性修复，而不是本轮回归。

### 显示与日志：不是 iOS 单线程，是 P8 控制台共用扫描缓冲

`DISPLAY stage=10 clients=69 frames=7`，`SWAP check=7 transfer=7 rc=0`；帧发生在约 188 s、219 s 和 387–399 s。UI 能出帧，但更新很少。

根因在 XNU 树里的 **P8 framebuffer console**：

- `patches/xnu/0003-cheeseburger-visible-early-console.patch` 把 `p8_console_char()` 写进
  `osfmk/console/serial_console.c`；
- `patches/xnu/0004-cheeseburger-console-handoff.patch` 又在 `vcputc_options()` 和
  `serial_putc_crlf()` 里调用它；
- `p8_console_char()` 直接写 `p6_diag_record[16]` 指向的 1080×1920 GOP 扫描缓冲，
  每个字符 16×32 像素，环形回绕或 `row>=60` 时把整屏清成黑色；
- 这条路径不看 `-v`、`debug=0x8` 或 `-enable_kprintf_spam`，所以去掉这些参数后日志仍会画屏；
- 同一块扫描缓冲没有第二个平面或合成器：iOS 的帧和内核文本只能互相覆盖，不能叠加。

因此“日志和系统画面单线程、日志不能盖在界面上”是当前板级 console 的实现结果，不是 iOS 显示栈的固有限制。

### PosterBoard 崩溃循环仍在

- 本轮 `Too many corpses` 36 次，`EXITS total=720 nonzero=249`；ENOSPC 为 0，说明崩溃循环不是 1 GiB Data 写满造成的。
- sandbox 仍拒绝 `IOPlatformDevice` 的 `canvas-width`/`canvas-height` 和
  `NativeLegacyDisplay` 的 `GainMapVersion`/`IOMFBIntDcpUsedForExtWhenLidClose`。
- 下一步应查这些属性在真实 IOMFB/IOPlatformDevice 上的来源和 sandbox profile，而不是继续加 Data。

## 可选方案

1. **关闭屏幕绘制，保留日志环（推荐）**：给 `p8_console_char()` 增加 `p8-fb-console` 开关；
   `0` 时只写 `p8_console_ring`，不碰 framebuffer。日志仍进持久日志窗口
   （`[visible-console-tail]`）和 CBHB，UI 独占屏幕。已在本轮后加入
   `patches/xnu/0008-cheeseburger-fb-console-switch.patch`，boot args 加 `p8-fb-console=0`。
2. **保留屏幕日志**：继续当前行为，适合纯内核调试，但 UI 会被覆盖。
3. **真正叠加**：在显示后端每次 swap 后把日志画到角落，或走 MDP5 独立 overlay 平面。
   这需要显示路径/合成器工作，当前阶段不做；且不能用内核 console 直接写扫描缓冲来冒充叠加。

## 工具问题

本轮 monitor 用 Python 3.9 调 `read-sheng-log.py --decode` 失败；窗口本身完整。
改用 `/opt/homebrew/bin/python3.13` 读取成功。后续 reader/monitor 固定使用 3.13，
不要用系统 3.9 的 `python3`。
