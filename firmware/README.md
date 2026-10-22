# 固件集成状态

P1 不包含可运行的 cheeseburger 固件适配器。原 edk2-msm8998 目录合同与 Mu-Silicium 不同，且 FD 已含 ARM64 镜像头。

先复现原 Linux/GCC5 离线构建，再用独立 UniXNU EFI 验证协议；需要时才修改 DSC/FDF。具体差距和验收见 `../docs/MSM8998_UEFI_GAP.md`。

不要创建带猜测地址的 Resources/Configs/cheeseburger.toml，也不要复制 sheng 的内存图、vendor DXE 或 Trust Cache 数据来绕过门禁。
