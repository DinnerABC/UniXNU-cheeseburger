# P21 编译期内置启动参数

启动参数通过设备树字符串传给 XNU，串太长（实测超过约 700 字节）时尾部会被截断，
且每个参数都要在 payload 配置里手工维护。为缩短参数串并固定板级行为，
**只被本仓库代码读取**的参数改为编译期内置：`package/BoardRuntime/BoardBootArgs.cpp`
里的 `kDefaults[]` 提供默认值，`cb_board_arg()` 先读启动参数、读不到才用默认值。
因此仍可在启动参数里单独覆盖某一项而不必重新编译。

## 内置参数（kDefaults）

| 参数 | 默认值 | 读取位置 | 说明 |
| --- | --- | --- | --- |
| `cheeseburger-touch` | 1 | `CheeseburgerTouch.cpp` | 启动 S3320 触摸采集线程 |
| `cheeseburger-buttons` | 1 | `CheeseburgerButtons.cpp` | 电源/音量按键适配 |
| `cheeseburger-home-chord` | 1 | `CheeseburgerButtons.cpp` | 音量键组合当 Home |
| `sheng-touch-hid` | 1 | `HidInputProbe.cpp`, `NativeHidInput.cpp` | 键盘+digitizer 合并 HID 设备（实机上事件驱动只附着这个形态） |
| `offline-hid-input` | 1 | `HidInputProbe.cpp` | 启动离线 HID 探测 |
| `offline-hid-home` | 1 | `HidInputProbe.cpp` | 允许离线设备发送 Home 报告 |
| `sheng-aks` | 1 | `KeyStoreProbe.cpp` | AKS 兼容层 |
| `sheng-aks-sls` | 3 | `KeyStoreProbe.cpp` | AKS 兼容层模式 |
| `offline-key-events` | 1 | `KeyStoreProbe.cpp` | 离线按键事件 |
| `sheng-log` | 1 | `NativeUfs.cpp`, `BootLog.c` | 持久日志窗口 |
| `p20-observe` | 1 | `CompactObserver.c` | 进程/显示观察器 |
| `p21-iokit-trace` | 1 | `IOKitMatchTrace.cpp` | IOKit 匹配追踪 |
| `cheeseburger-upstream-userland` | 1 | `BootWatchdogPolicy.c` | 使用上游用户态（不做手机身份） |
| `cheeseburger-disable-watchdogd` | 1 | `BootWatchdogPolicy.c` | 关闭 watchdogd 策略 |

新增或修改内置参数时：改 `kDefaults[]`、更新本表、提交；不需要动 payload 配置。

## 仍保留在启动参数里的项

这些参数被 **官方框架/内核早期代码** 直接读取，不能在 `package/BoardRuntime`
里拦截，必须留在 `config/unixnu.toml`：

- 内核早期：`-v`、`keepsyms=1`、`cpus=8`、`debug=0x8`、`startup_debug=0`、
  `kextlog=0xfff`、`-enable_kprintf_spam`
- 板级 SMP 补丁：`sheng-smp=1`、`armcpu-self-ipi=1`
- 官方显示驱动（`drivers/GopFramebuffer`、`drivers/NativeMobileDisplay`）：
  `sheng-vsync=60`、`sheng-vsync-fixed=1`、`offline-legacy-unit-scale`、
  `offline-legacy-register`、`offline-legacy-phase`、`offline-legacy-gop`、
  `offline-legacy-pm`、`offline-legacy-validate`、`offline-legacy-transfer`、
  `offline-legacy-cpu-present`、`offline-mobile-graphics-load`
- 诊断开关（默认关闭，需要时临时加）：
  `cheeseburger-touch-irq`、`cheeseburger-touch-transform`、`cheeseburger-usb-probe`、
  `cheeseburger-usb-mode`、`cheeseburger-disp-probe`、`sheng-fb-diff`、`sheng-fb-gpu`、
  `sheng-early-retire`、`sheng-touch-split`、`offline-legacy-retire`、
  `armcpu-diagnostics`

当前参数串长度：**428 字节**（内置前为 714 字节）。探针类参数放在串首，避免尾部被截断。
