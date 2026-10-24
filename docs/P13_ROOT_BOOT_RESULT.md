# P13 根卷启动结果

## 已证实的写入

固定 P12 根镜像 9,382,658,048 bytes 已一次性完整刷入 47,938,797,568-byte userdata：68/68 成功，退出码0。原 Recovery 对同样长度做设备端 SHA256，与 P12 的 `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093` 一致。userdata 容量未超限。见 flash-result.json、readback.json。

## 临时启动阻塞

完整 P12 boot.img 为131,112,960 bytes，RAM bundle 为124,993,536 bytes。首次发送和 Booting 均返回 OKAY，但 Fastboot 持续在线，用户仍见 Fastboot 画面。随后从 Recovery 进入全新 Bootloader 会话，完整镜像再进行一次有因对照，仍停留 Fastboot。两次均未取得 UEFI/XNU 根挂载执行证据，不再重复发送同一完整镜像。

同一 P12 FD/DTB/header 的6,127,616-byte小包仅含8,192-byte配置载荷、不含XNU。原温态会话出现00000050加载/认证错误；重启Bootloader后同一小包实际进入EFI Shell，由用户确认。说明小包能执行，温态会话也会影响结果。不能据此断定精确64MiB执行上限；max-download-size=768MiB只是传输能力。

目前可定位到完整RAM载荷相关的早期加载路径，尚未定位厂商Bootloader第一条失败指令。不能把Fastboot OKAY、Recovery读取SHNGROOT头或镜像SHA匹配当成XNU已识别manifest、通过VerifiedRoot、发布IOMedia或挂载APFS。上述实机阶段均未确认，launchd亦未进入。

## P13 无损压缩适配

完整原始P12bundle已压缩为74,756,995-byte LZMA流，加固定身份封装后74,757,051 bytes。Python无损往返与固定EDK2 SDK C解码器宿主测试均通过；截断和错误输出长度被宿主测试拒绝。内核、ADT、配置、Trust Cache逐字节不变。宿主测试不能替代UEFI实机解压证据。

P13独立UEFI覆盖层加入固定EDK2解码库及展开后SHA256门禁；保留原FDT/内存范围、PEI保留HOB、bundle CRC/路径/越界检查。只修改P13源码层，不修改P12。当前构建/打包状态以out/p13/logs和compressed/中的实际结果为准；未通过门禁不发送。

## userdata 内部引导区域备选

可在现有userdata内部的根镜像之后定义独立256MiB只读引导文件窗口，无需新增GPT分区。离线计算见boot-files-window-proposal.json；原OS/SystemOS及SHNGROOT窗口不动。此方案仍需真实UEFI BlockIO读取、边界和哈希校验、原子部署/回读验证，目前没有实现或写入。P13原授权仅指定已验收P12根镜像，追加新窗口需单独明确授权；不得因此修改GPT。

P12无Data seed，原Apple根认证保持。即使解决RAM启动问题，也不能承诺直接进入完整用户空间。内核USB/UART电子日志通道尚未建立。

## 新压缩候选实际测试

独立UEFI构建退出码0，AArch64 PE/FV集成及固件/DTB回读验证通过。新增压缩镜像为80,887,808 bytes，SHA256 `02f393443c7d37c2c2c4a8f9107be6c05f858023171bcd23b35712afe8da73bf`。固定展开后P12bundle的SHA不变。

已在确认目标身份和解锁状态、重启Bootloader建立新会话后，临时发送一次，命令退出码0。没有写任何分区。两次有限只读观察显示 Fastboot 和 ADB 均已离线，与完整原包持续Fastboot在线的表现不同；这只证明USB状态改变，不证明解压成功或内核执行。当前等待屏幕证据，不自动重复启动。见compressed/boot-result.json、usb-observation.json。

## 最新实机结果：已进入 XNU，根挂载后认证失败

用户在新压缩候选上报告 First Fatal：`rootvp not authenticated after mounting @bsd_init.c:946`，Last bootstrap 为 bsd_init。原始OCR保存在 `out/p13/compressed/screen-rootauth-01.txt`。根据固定 P12 内核符号表，规范化后的 Caller `0xfffffff00bfb0b8c` 精确落在 `_bsd_init.cold.5`；OCR地址仍不是电子采集。归因见 `compressed/rootauth-analysis.json`。

核对实际 bsd_init.c 控制流：vfs_mountroot() 必须返回0，VFS_ROOT() 必须成功，rootvnode 已设置，才会执行 VNOP_IOCTL(rootvnode, FSIOC_KERNEL_ROOTAUTH, ...) 并因非零返回值触发该 panic。因此有可信实机证据确认 XNU 已运行至 VFS 根挂载之后；原来的 Fastboot 停留已由新压缩临时启动方案突破。没有修改内核、根镜像或认证逻辑。

证据边界：本次没有拍到P13解压SHA页面；不单独宣称该页面已观察到。未回收数值 autherr、实际运行时 filesystem type/卷UUID、VerifiedRoot读计数和完整IOMedia注册树，尚不能将上述各项全部填写为独立日志验证成功。目标卷离线已确定为APFS，实际本次根vnode的文件系统类型仍待运行时输出确认。launchd未进入。屏幕明确说明异常寄存器可能陈旧，不能用这些值诊断新的CPU异常。

下一处真实故障是Apple根卷认证接口返回非零，具体errno和文件系统认证条件尚未捕获。P12所有者RSA块策略/整镜像哈希与Apple FSIOC_KERNEL_ROOTAUTH是不同契约，前者成功不替代后者。固定上游的offline-owner-root-binding-audit补丁包含在Apple认证失败后另行接受owner策略的路径；本轮没有应用该策略替代，也没有清零autherr、跳过sealedness或伪造Apple成功。

下一轮应在原认证调用处保留原返回值和panic，捕获autherr、root mount文件系统名、根设备及卷身份，再与原APFS组件实际认证条件核对。对需要的原始manifest/封印/身份输入做真实性与版本一致性校验。不能在没有错误码和原始证据时直接猜测缺某项签名。

手机目前为用户报告的内核First Fatal停机现场；已保存文字，不再发启动/刷写命令。继续实机前需要人工恢复到Recovery，但无需重刷现有userdata。
