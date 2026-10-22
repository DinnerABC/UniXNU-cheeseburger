# P3 分区备份清单

已在设备原有 Recovery 的 root ADB 下流式读取。所有镜像仅在项目 `backups/p3/`，被 Git 忽略，不上传。原始命令、时间、映射和退出码见各目录 manifest.json；私有原始检测日志在 out/p3/private。

每个下列镜像均满足：读取命令退出 0、实际大小等于 blockdev --getsize64、读取前和读取后设备 SHA256 等于 Mac SHA256。镜像权限 0600。exec-out 不采用交互终端，不混入 dd 统计文本；不能仅凭传输退出 0 判断完整性。

| Actual partition | Block device | Bytes | SHA256 | Project-relative file |
| --- | --- | ---: | --- | --- |
| persist | /dev/block/sda2 | 33554432 | ad2caacfc0b70b3f2edc71c5d31aa6f680f5de5db9ae5c9c6acf0684ad081ec0 | backups/p3/20261022-023530/persist.img |
| modemst1 | /dev/block/sdf1 | 2097152 | 10da7335894548b7771ae2ea17ed9582db12ea62618a8b5555ec329fd8962c48 | backups/p3/20261022-023530/modemst1.img |
| modemst2 | /dev/block/sdf2 | 2097152 | 93e4f6e565eec7542e2a6eefb72ec1badbefa0842b013c0bd1076c93d311bc53 | backups/p3/20261022-023530/modemst2.img |
| fsg | /dev/block/sdf3 | 2097152 | 5647f05ec18958947d32874eeb788fa396a05d0bab7c1b71f112ceb7e9b31eee | backups/p3/20261022-023530/fsg.img |
| fsc | /dev/block/sdf4 | 4096 | ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7 | backups/p3/20261022-023530/fsc.img |
| oem_dycnvbk | /dev/block/sda7 | 10485760 | 53d7d90aa1a21421022f4870d68728c892db6042d8bec5fc1f35335854e3f60a | backups/p3/20261022-023530/oem_dycnvbk.img |
| oem_stanvbk | /dev/block/sda8 | 10485760 | 8c94a1bb9bc0c44d5db60a68f17938344cebacf27123312da2c140c998a72115 | backups/p3/20261022-023530/oem_stanvbk.img |
| param | /dev/block/sda9 | 1048576 | fd9e08c9aff0be42508365f09564b92a6ddbe5bf97bbedee817af80f688c69a0 | backups/p3/20261022-023530/param.img |
| config | /dev/block/sda12 | 524288 | d18e477fbcaaac6844d28bafffbea6719b7052700c7ed287a023f57476b7f2a2 | backups/p3/20261022-023530/config.img |
| keystore | /dev/block/sda5 | 524288 | 07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541 | backups/p3/20261022-023530/keystore.img |
| frp | /dev/block/sda6 | 524288 | 07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541 | backups/p3/20261022-023530/frp.img |
| ssd | /dev/block/sda1 | 8192 | 9f1dcbc35c350d6027f98be0f5c8b43b42ca52b7604459c0c42be3aa88913d47 | backups/p3/20261022-023530/ssd.img |
| boot | /dev/block/sde19 | 67108864 | 366d8208c0b9f47ec90fc19e73e0cd0efab1f85ee01980e1f447f59ec4e1c41d | backups/p3/20261022-023530/boot.img |
| recovery | /dev/block/sde22 | 67108864 | 296ceaf2738e75659b8b7e74c045cfa5f5afd09945b7fdb67057ad90cf7f0c41 | backups/p3/20261022-023530/recovery.img |
| abl | /dev/block/sde16 | 8388608 | 393f099adc56d569d00cf12affc2fd38d7e9a12b487a4652590b7628d4d7a0b4 | backups/p3/20261022-023615/abl.img |
| ablbak | /dev/block/sde17 | 8388608 | 154a437f15775cb76c69a5e1dfc8c6d0f378db4caf158b216cbde8be049d8c5a | backups/p3/20261022-023615/ablbak.img |
| xbl | /dev/block/sdb1 | 4149248 | 84a0f2980174d21c1c28b837cf2ca6211396542c3de1e558bb7632f44fb4ea90 | backups/p3/20261022-023615/xbl.img |
| xblbak | /dev/block/sdc1 | 4149248 | 3d0f36751a410fa41c9bab81be1c92752b2a59edaa32f3597a7586a5e05a1dd2 | backups/p3/20261022-023615/xblbak.img |
| tz | /dev/block/sde3 | 2097152 | f59bf33f88bc1fcbecb4047ac626ac7a05f7c5a7aa0a6ef6ca2d441081f2b38a | backups/p3/20261022-023615/tz.img |
| tzbak | /dev/block/sde4 | 2097152 | 6d1a85a5bb9c468a7c5632e426fa28be393e8ff6cfbe794354a2d39dc76c4d00 | backups/p3/20261022-023615/tzbak.img |
| rpm | /dev/block/sde1 | 524288 | be9ddf81c1aa3e15de6727b6abf364cb9558577b0fa2a0a0c7d9a99dadac5f19 | backups/p3/20261022-023615/rpm.img |
| rpmbak | /dev/block/sde2 | 524288 | d231a22b427a3f4ba72cc7231c1cbe51eff38e03bc3c6aa6d96df22c9bb95504 | backups/p3/20261022-023615/rpmbak.img |
| hyp | /dev/block/sde5 | 524288 | 89cb3f2891096fc50b9f66e40373d2e2dda899ba4f623c52227246d52a725485 | backups/p3/20261022-023615/hyp.img |
| hypbak | /dev/block/sde6 | 524288 | 3c3c9811ff152de1805157add7623310c9852e81b10d7e32284c77564edd3b76 | backups/p3/20261022-023615/hypbak.img |
| devcfg | /dev/block/sde39 | 131072 | 6b0336870f4678f0682ab3d86c7731e4a18842bcf29138bf132e20348931f1f9 | backups/p3/20261022-023615/devcfg.img |
| sec | /dev/block/sde7 | 16384 | 7d38913cbd259ba6942bcf3a8ce77c64ab74cf80014cc9c7d6991c601c8f5637 | backups/p3/20261022-023615/sec.img |
| pmic | /dev/block/sde8 | 524288 | 66f500cf5bb10fe5df1a6cba5fd6fcc26cab960490461b7dfa71f9a30e6548a8 | backups/p3/20261022-023615/pmic.img |
| pmicbak | /dev/block/sde9 | 524288 | c01dc71dd127381a4b7cabdcc46585bd14fa416c4e6a248df3b834864e38f601 | backups/p3/20261022-023615/pmicbak.img |
| keymaster | /dev/block/sde25 | 524288 | 7efca709264e554eef199aa4d2fcb7c66031e3edddb0861677d0fb687f4e8857 | backups/p3/20261022-023615/keymaster.img |
| keymasterbak | /dev/block/sde26 | 524288 | 1d1114d2391b97ddd39c5d69f75851998c466af906bd37c7d330d54cf015f119 | backups/p3/20261022-023615/keymasterbak.img |
| cmnlib | /dev/block/sde27 | 524288 | 79a146423dab7ef3dd0223c9ed3a7fcd8e83fdcc940d5a85c9909268119434c4 | backups/p3/20261022-023615/cmnlib.img |
| cmnlibbak | /dev/block/sde28 | 524288 | b2c636b12470b8d68b99950c87756978f7435dc36ac390a6fde10fc6e75e1649 | backups/p3/20261022-023615/cmnlibbak.img |
| cmnlib64 | /dev/block/sde29 | 524288 | 883eb113b7d248e24d6abef630169910de000a30a28027582a74d12dc26f0a69 | backups/p3/20261022-023615/cmnlib64.img |
| cmnlib64bak | /dev/block/sde30 | 524288 | 1bff47d5d7e97530adc00b328925e4615d62c75c5039f2b1a7a0675b07752401 | backups/p3/20261022-023615/cmnlib64bak.img |
| modem | /dev/block/sde10 | 115343360 | 5af929da5eed67eae4c6e2f1304b056d73efd255de1a0ab271b26d376a072105 | backups/p3/20261022-023615/modem.img |
| dsp | /dev/block/sde11 | 16777216 | a380b3ed87753e564dc308e5afa583cf30ce82a0ce37e2d25cc1bd119d45c1b6 | backups/p3/20261022-023615/dsp.img |
| cdt | /dev/block/sdd2 | 4096 | ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7 | backups/p3/20261022-023615/cdt.img |
| ddr | /dev/block/sdd3 | 1048576 | b6279105d34c0260e8fcc4eb6d0a86fa61b62e91af25c0d1b6a444716be8a1b4 | backups/p3/20261022-023615/ddr.img |
| reserve1 | /dev/block/sda10 | 8290304 | 291a40b3ab50434387848c178044024f201fc1007bd1791796ff87c1d07e0442 | backups/p3/20261022-023615/reserve1.img |
| reserve2 | /dev/block/sda11 | 16580608 | abbc9df762772bc9ef7b2d212b0e585ea3c83b3df036a8f75fd7b6114155ae41 | backups/p3/20261022-023615/reserve2.img |
| reserve3 | /dev/block/sdf7 | 529891328 | ce5c05833e2de8ae66521dc3d73c0821375f6dc7888c256002373f4e3b0663b3 | backups/p3/20261022-023615/reserve3.img |
| devinfo | /dev/block/sde23 | 4096 | 13f0173448946aac9d42e90e8253758443229c62814765563a17fedebf9ecbac | backups/p3/20261022-025054/devinfo.img |
| misc | /dev/block/sda4 | 1048576 | 89e2dadc49213f32a9bcde3e733ea03a106bea617d58e7cc9997ac634ed3ea17 | backups/p3/20261022-025054/misc.img |
| limits | /dev/block/sde35 | 4096 | ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7 | backups/p3/20261022-025054/limits.img |
| apdp | /dev/block/sde31 | 262144 | ee22a44270b6ac34d4d2a9d52654eecc53ae225bbbd1ba2c09bc1b25b8c5cf26 | backups/p3/20261022-025054/apdp.img |
| msadp | /dev/block/sde32 | 262144 | 8a39d2abd3999ab73c34db2476849cddf303ce389b35826850f9a700589b4a90 | backups/p3/20261022-025054/msadp.img |
| dpo | /dev/block/sde33 | 4096 | ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7 | backups/p3/20261022-025054/dpo.img |
| storsec | /dev/block/sde41 | 131072 | 989c32056c416f064c0875a106848585cd233f0043cafaa693ac8461aa2b644d | backups/p3/20261022-025054/storsec.img |
| storsecbak | /dev/block/sde42 | 131072 | 989c32056c416f064c0875a106848585cd233f0043cafaa693ac8461aa2b644d | backups/p3/20261022-025054/storsecbak.img |
| mdtpsecapp | /dev/block/sde12 | 4194304 | 125ee4bf59550a438f62a4d2707e5b7e155c1fc0632bb1f83f83878f3ccffc86 | backups/p3/20261022-025054/mdtpsecapp.img |
| mdtpsecappbak | /dev/block/sde13 | 4194304 | 125ee4bf59550a438f62a4d2707e5b7e155c1fc0632bb1f83f83878f3ccffc86 | backups/p3/20261022-025054/mdtpsecappbak.img |
| mdtp | /dev/block/sde15 | 33554432 | c47821e639220f11a04e6d03dc2f6e67fe73dcb8738ffd1968462ea98a21c7d6 | backups/p3/20261022-025054/mdtp.img |
| dip | /dev/block/sde14 | 1048576 | 30e14955ebf1352266dc2ff8067e68104607e750abb9d3b36582b8af909fcb58 | backups/p3/20261022-025054/dip.img |
| bluetooth | /dev/block/sde24 | 1048576 | 321b03c948816c40554bb14032729f5c00dedcaba5a39f3f047d999595545893 | backups/p3/20261022-025054/bluetooth.img |
| toolsfv | /dev/block/sde36 | 1048576 | 30e14955ebf1352266dc2ff8067e68104607e750abb9d3b36582b8af909fcb58 | backups/p3/20261022-025054/toolsfv.img |
| sti | /dev/block/sde38 | 2097152 | 5647f05ec18958947d32874eeb788fa396a05d0bab7c1b71f112ceb7e9b31eee | backups/p3/20261022-025054/sti.img |
| splash | /dev/block/sde34 | 34226176 | 7283ab986bc6374f080972e00271ca3d1b98957933927b38f4901293d069ea33 | backups/p3/20261022-025054/splash.img |
| LOGO | /dev/block/sde18 | 16777216 | f6d7b99f0f9fb2808accbca8b2818a73f14c0fb1adad6f605dae9678fd411b58 | backups/p3/20261022-025054/LOGO.img |

## GPT

6 个 LUN 均实际查询逻辑扇区大小（4096），按 GPT header 给出的 entry LBA/count 读取。主备 header CRC、entry array CRC、主备数组相同、重复读取相同均验证通过。保留 protective MBR 和两个 GPT 的原始区间；这些是区间镜像，不是整个 LUN 镜像，不提供写回命令。

| LUN | 区间起始 LBA | 字节 | SHA256 | 项目内文件 |
| --- | ---: | ---: | --- | --- |
| sda | 0 | 12288 | 03cc8961d806345d8baf40301c503c5667b91840c80ebcc9b7f947786dd2b266 | backups/p3/gpt-20261022-023658/sda-primary.bin |
| sda | 14145534 | 8192 | 8d3fa0d5735d345187c63755c02e087eef89117f7882d2145bfb1716c7a332a8 | backups/p3/gpt-20261022-023658/sda-backup.bin |
| sdb | 0 | 12288 | 147a5e7add00eecc4f63dc813299bcd26565fa8769a914d93e23a766e8a14673 | backups/p3/gpt-20261022-023658/sdb-primary.bin |
| sdb | 1019 | 20480 | bc39bf0868490e93e524ad84335ce1322048fb01ec680241872cb20f5b3109f4 | backups/p3/gpt-20261022-023658/sdb-backup.bin |
| sdc | 0 | 12288 | 7fab7738b4ecd3b0585570a64c2bb43d6d5308b8904f1b0e2519fa36b6f473e4 | backups/p3/gpt-20261022-023658/sdc-primary.bin |
| sdc | 1019 | 20480 | 50c92e7bbb668d2757f14adecdc6499e15b83ab78deb9db5778e72dc16287e56 | backups/p3/gpt-20261022-023658/sdc-backup.bin |
| sdd | 0 | 12288 | f4241a9688ce4f9e302b9f00077928418e7ed2656211e0a37aa6e1b638835160 | backups/p3/gpt-20261022-023658/sdd-primary.bin |
| sdd | 32763 | 20480 | 9f0960ab051aac099484c828e91559ef1e6562eabc8a223f36096506feb08869 | backups/p3/gpt-20261022-023658/sdd-backup.bin |
| sde | 0 | 16384 | d6827075f66aa7c853e4d71267590cf56c8ea6c0998138f5863bf625d21fb57a | backups/p3/gpt-20261022-023658/sde-primary.bin |
| sde | 1048573 | 12288 | dd6c486a53bd1ed24fa0a9ac80f2dbaefbe5a27d00a8b15b618b41feffb89e72 | backups/p3/gpt-20261022-023658/sde-backup.bin |
| sdf | 0 | 12288 | 45a995b8281e87a0fd28aa1c4cf0ade9226a11c5f285829499073d21af20705d | backups/p3/gpt-20261022-023658/sdf-primary.bin |
| sdf | 393183 | 135168 | 6503a9f3ba764062a04dd33e0d8e86d43322fa9d7e40f899cdf386d8ad72ae0b | backups/p3/gpt-20261022-023658/sdf-backup.bin |

## 范围与恢复限制

尚未完整备份 system、vendor、userdata；这不是整机可回滚快照。dtbo 在实际 by-name 中未发现，没有伪造该备份。现有 Android 和 Recovery 均已真实进入；boot/recovery 固件、校准数据与 GPT 已保留。没有执行刷写、解锁、锁定、格式化或清除数据。Recovery 自己的挂载表显示 data/cache 为 rw；本工程没有发起挂载或分区写入。
