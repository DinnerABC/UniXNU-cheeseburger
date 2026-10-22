# P1 本次证据

本目录保存恢复磁盘后在项目内冷构建的证据。current-run 是 out 日志和报告的快照；efi 是实际 5 个 EFI 的副本，使用上游 UniXNU 许可（UPSTREAM_LICENSE）。

source-provenance.json 包含固定源 SHA、gitlink 和 tracked diff；msm8998-binary-inventory.json 只有 vendor blob 元数据，没有复制 blob。validation.json 区分真实退出码与预期门禁失败。shellcheck-initial.log 保留修正脚本前的告警，当前 shellcheck.log 为修正后结果。

所有 result 的 hardware_validated 均为 false。这里没有设备固件、XNU、boot.img 或手机运行通过的证据。SHA256SUMS.json 校验本证据目录中的文件。
