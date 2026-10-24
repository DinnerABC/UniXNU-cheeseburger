# P13 续接现场

userdata 已写入一次且设备端完整根镜像前缀SHA匹配：读取out/p13/flash-result.json、readback.json。禁止重刷userdata。用户取消了原userdata备份，没有可用副本；P3关键备份仍在，Android恢复目标仅为空数据系统。

当前手机由用户报告仍在Fastboot。完整P12临时boot在初次与干净会话各一次均未进入可确认UEFI/XNU，不再发送相同完整镜像。小型同FD配置包经Bootloader重启后已进入Shell。问题是完整RAM载荷早期加载路径，非userdata容量超限；尚未证明具体Bootloader大小限制。

正在处理P13无损压缩UEFI：scripts/p13-guest-overlay.sh、p13-prepare-compressed-uefi.py，config/p13/CompressedPayload.h，patches/p13/compressed-transport.patch。固定P12bundle压缩约75MB，Python及固定SDK C宿主还原SHA通过。先读out/p13/logs/build-compressed-uefi-01.*，检查真实构建退出码，再导出固件、验证库/PE/FV、打包回读与SHA，合格后只临时启动新候选。需先清理温态会话的影响；有限一次bootloader重启，不无限测试。

UEFI VM仅4GiB，现为P13独立overlay，P12及历史层保留；未重编XNU。新源码与产物均在项目内。

userdata内额外引导文件窗口仅为离线备选：out/p13/boot-files-window-proposal.json。不修改GPT，不改变现有根卷窗口；尚无真实BlockIO读取实现，尚未取得追加窗口写入的独立明确授权，不自动写手机。

SHNGROOT/VerifiedRoot/IOMedia/APFS/launchd的本轮XNU实机验证仍未发生。保留原安全验证，不伪造成功。

## 最新状态：新压缩版已发送一次

build-compressed-uefi-01、fixed-decoder-test-01、pack-compressed-01、boot-compressed-once-01均退出码0。新镜像out/p13/compressed/uefi/boot-p13-compressed.img，80,887,808 bytes，SHA256 02f393443c7d37c2c2c4a8f9107be6c05f858023171bcd23b35712afe8da73bf。不得再次运行同名/同镜像启动脚本。

发送后两次USB观察：ADB=false，Fastboot=false。此状态不是内核成功证据。已向用户请求当前屏幕文字，先保留现场，不自动重启。等真实屏幕判定解压、UEFI、XNU和根卷阶段。新GPT/额外userdata窗口仍没有写入。

## 最新实机结果：已进入 XNU，根挂载后认证失败

用户在新压缩候选上报告 First Fatal：`rootvp not authenticated after mounting @bsd_init.c:946`，Last bootstrap 为 bsd_init。原始OCR保存在 `out/p13/compressed/screen-rootauth-01.txt`。根据固定 P12 内核符号表，规范化后的 Caller `0xfffffff00bfb0b8c` 精确落在 `_bsd_init.cold.5`；OCR地址仍不是电子采集。归因见 `compressed/rootauth-analysis.json`。

核对实际 bsd_init.c 控制流：vfs_mountroot() 必须返回0，VFS_ROOT() 必须成功，rootvnode 已设置，才会执行 VNOP_IOCTL(rootvnode, FSIOC_KERNEL_ROOTAUTH, ...) 并因非零返回值触发该 panic。因此有可信实机证据确认 XNU 已运行至 VFS 根挂载之后；原来的 Fastboot 停留已由新压缩临时启动方案突破。没有修改内核、根镜像或认证逻辑。

证据边界：本次没有拍到P13解压SHA页面；不单独宣称该页面已观察到。未回收数值 autherr、实际运行时 filesystem type/卷UUID、VerifiedRoot读计数和完整IOMedia注册树，尚不能将上述各项全部填写为独立日志验证成功。目标卷离线已确定为APFS，实际本次根vnode的文件系统类型仍待运行时输出确认。launchd未进入。屏幕明确说明异常寄存器可能陈旧，不能用这些值诊断新的CPU异常。

下一处真实故障是Apple根卷认证接口返回非零，具体errno和文件系统认证条件尚未捕获。P12所有者RSA块策略/整镜像哈希与Apple FSIOC_KERNEL_ROOTAUTH是不同契约，前者成功不替代后者。固定上游的offline-owner-root-binding-audit补丁包含在Apple认证失败后另行接受owner策略的路径；本轮没有应用该策略替代，也没有清零autherr、跳过sealedness或伪造Apple成功。

下一轮应在原认证调用处保留原返回值和panic，捕获autherr、root mount文件系统名、根设备及卷身份，再与原APFS组件实际认证条件核对。对需要的原始manifest/封印/身份输入做真实性与版本一致性校验。不能在没有错误码和原始证据时直接猜测缺某项签名。

手机目前为用户报告的内核First Fatal停机现场；已保存文字，不再发启动/刷写命令。继续实机前需要人工恢复到Recovery，但无需重刷现有userdata。
