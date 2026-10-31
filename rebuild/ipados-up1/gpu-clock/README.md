# GPU 时钟/电源域只读清点（UEFI）

对应官方 `/zh/porting/gpu` 第 2 步“适配电源和固件输入”的前置取证。本补丁
**不使能任何时钟、电源域或复位**，只通过固件自己的 `EFI_CLOCK_PROTOCOL`
查询 A540 相关资源和当前状态。

## 依据

- 本机只读探针（内核 `dd0e4ad9`）：GCC GPU AHB/BIMC 已开、GPUCC 可读，但
  CX/GX GDSC 与所有 GPUCC 分支均关闭（`20261031T050242Z`）。
- 固件 `ClockDxe` 字符串含 `VDD_GPU_CX`、`VDD_GPU_GX`、
  `VDD_HLOS1/2_GPU_SMMU`、`gcc_gpu_*` 与 `gpucc_*` 时钟名；说明固件自己拥有
  这些资源与 RPM 投票。
- 协议头与 GUID 来自本机 `upstream/uefi-sheng`
  `Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIClock.h`：
  `EFI_CLOCK_PROTOCOL_GUID = 241AFAE6-885F-4F6C-A7EA-C28EAB79C3E5`，
  版本 `0x10009`；提供 `GetClockID`、`IsClockEnabled/On`、`GetClockFreqHz`、
  `GetClockPowerDomainID`、`EnableClock(PowerDomain)`、`ResetClock` 等。

## 实现

- `UniXNUGpuClock.c`：定位协议，按名查询 8 个 GPU 时钟（ID/使能/在跑/频率/返回码）
  与 4 个电源域 ID；结果拼成一行，经 `RailWriteCapture` 写入日志窗口尾部
  保留块（LBA 5071073 / userdata 相对 2629601），同时打印到控制台。
- `apply.py`：把 `EFIClock.h`、`UniXNUGpuClock.c` 放进
  `UniXNUPkg/Driver/UniXNUDxe`，把 `UniXNURails.c` 的 `RailWriteCapture`
  去掉 `STATIC`，在 INF 增加源文件，并在 `UniXNUDxe.c` 的触摸供电调用之后
  调用 `UniXnuGpuClockProbe()`。

## 判读

回收日志后从 `window.bin` 的块 8161（4096 字节）读取 `UP1GPU1 ...` 行：

- `clock=0` 且 `ver=10009`：Clock 协议可用；
- 每个时钟的 `GetClockID` 返回 0 且 ID 非 0：名称与固件匹配；
- `VDD_GPU_CX/GX` 的域 ID 与返回码给出下一步上电句柄；
- 任何时钟 `en=1/on=1` 说明固件已经为它投票，上电顺序可据此缩减。

## 回滚

本目录只新增源文件并改 INF/入口调用；恢复固件源码到上一 overlay 层或删除
`UniXNUPkg/Driver/UniXNUDxe` 下这三个文件即可。
