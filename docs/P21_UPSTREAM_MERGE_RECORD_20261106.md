# 上游解法与本机合并记录 · 2026-11-06

优先资料根：`/Volumes/NAS-MacData/Project/IOS/upstream`。固定提交而非“最新”字样作为来源。
以下区分已合并、离线验证、实机结果和因缺证据暂不合并；不复制其他板级地址或ABI。

| 问题 | 上游依据 | 本机处理 | 验证与限制 |
|---|---|---|---|
| 日志boot归属与保留原始窗口 | UniXNU-sheng `scripts/read-sheng-log.py` header/decode；固定官方框架 `16e592abee8a550dec8154ed1f38b5ebc4f16213` log-capture指南 | 保留官方解码入口，在现有SHLOG/CBHB外围加独立有效信息分析，保留原窗口与旧报告 | 两个新boot均35有效槽、0写错；非当前boot/无效记录不混入 |
| CORE假0与rc/stage重叠 | 本机源码缺陷；上游按Result/Record分别表达状态，不提供可直接套用补丁 | 增加独立signed rc/fence/stage和有效位，修正赋值，首次drain失败锁存 | 实机CORE2 valid5/rc-4与原msgbuf一致；post/fence明确未执行 |
| 时间原点与假gap | 现有writer/heartbeats各自相对writer start；官方decoder保留原始秒和boot | 增加CNTPCT/CNTFRQ LOGTIME并归一；按producer子环而非tag计算缺口 | 旧171/168秒不等于内核时间；29项日志模型测试通过 |
| GPU完成收尾后停止 | Linux `adc218676eef25575469234709c2d87185ca223a`, `a5xx_gpu.c::_a5xx_check_idle/a5xx_idle`；sagit `673f85070fe9f7872793015eb230a5c3987d4a8c`, `AdrenoSubmit.h::idlePowerTransition` | 复用有界idle等待到halt前，仍保留原mask及Status3/游标最终检查 | 第二轮preidle通过但halt后仍bit30 busy；仅等待不足以解决，不宣称已修GPU |
| halt后RBBM_STATUS bit30 | 相同Linux源 `registers/adreno/a5xx.xml`，GPU_BUSY_IGN_AHB_CP；sagit `haltedPipelineIdle` 同样拒绝bit30 | 保留拒绝与隔离，不扩大mask | 实机c00001c1、ACK和游标正常；能定位busy但不能分离CP halt/VBIF halt作用 |
| RBCCU/SP恢复 | Linux `a5xx_pm_resume` L1351–1394；Lineage vendor `d152ec623891d141ad5231ef5033c068e82ba1b1`, `adreno_a5xx.c::a5xx_regulator_enable/_poll_gdsc_status`；sagit `AdrenoStartup.h::enableCoreBlocks` | 记录RBCCU→3微秒→ready→SP→ready；更正3ms和“认证后必然pm_resume”错误描述 | 上游restart/resume前提不同；未找到本板保留运行环CP-only恢复案例，暂不合并新电源顺序 |
| Metal编译需要A540后端 | sagit `tools/gpu-compiler/{Compiler.cpp,Backend.c,AIRNIR.cpp}`，Mesa24.3.4 `769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de` | 本地隔离构建AIR→NIR→IR3，固定gpu540；wrapper验证哈希/621word反射/代码 | 真实21H461 CS/VS/FS编译通过；无Vulkan依赖，不等于设备执行 |
| 动态compute状态和绑定 | sagit `AdrenoPipeline.h::dispatch/constants/buffers/shaderObject/loadShader`；`GPUShaderProtocol.h`/`GPUJITProtocol.h` | 按上游语义适配typedmetadata与SSBO/constant encoder，继续本机NativeAdrenoABI | 上游源码复用范围和实际测试见后续章节；不引入iOS18私有IOGPU selectors |
| metal2vulkan运行时 | 作者 `steelbrain/metal2vulkan` @ `219f01365dc4c8eb346816c405b5b17403793764`，runtime create/compiles/submit；sheng AdrenoMetal+Turnip | 保留为对照，不纳入A540运行时主线 | VK1.2+timeline需要成熟ICD，固定Turnip仅6/7/8代；A540需新后端，IOSurface/Metal私有ABI仍需本机实现 |

## 2026-11-06 后续真实数据流与 AIR 对象链

复用固定sagitCompiler反射组合、AIR/NIRcompiler、AdrenoPipeline多buffer/constants/
dispatch和QcomMTL库hash/资源持有/queue语义，适配本机NativeAdrenoABI。
已实机验证双SSBO真实读/XOR/add/write256，并找到原21H461MLCompute padding_gradient
真实AIRentry，严格exporter不改reflection通过，设备copy256+source/params/allguards通过。

新增受控NSObject对象链newLibraryWithData/newFunction/computePSO/buffers/queue/encoder/
commitwait只接fixedmetallibhash+entry，实际设备M540status4/v256/in780。没有复制iOS18
_limits或注册systemMetalDevice。目标实际objc类型存在/签名已采集，semanticlayout仍需核对。

来源、镜像、boot、guard归属和仍缺的system/graphics支持见
`docs/P21_A540_AIR_OBJECTS_20261106.md`、`docs/P21_A540_TRANSFORM_20261106.md`。

## 2026-11-06 真实写入阻塞修复与成功验证

固定sagit AdrenoPipeline::buffers和Mesa fd5_emit::emit_ssbos均要求
A5XX_SSBO_1_0_WIDTH(bytes&0xffff)，本机漏用宏，把256B写成word0100（WIDTH0/FMT1）。
已修UserlandCompute与kernelCompute，测试解码位域。实际单组64/64与四组256/256写入、
全部guard校验成功；恢复默认SPTPcollapse仍256/256。此前内部状态阻塞不再成立，
关键是SSBO尺寸移植遗漏，而非需要修改安全mask或禁用电源管理。

成功候选/日志/sha详 `docs/P21_A540_SSBO_WIDTH_20261106.md`。新参考 `ios-sailfish`
已拉取HEAD6f27e41f…，其WIDTH同高16位提供交叉支持，但本机修复来自此前固定sagit/Mesa。
它走A530GLES不是Metal，不切换当前路线。

## 无人值守 sagit 后续迁移记录

完整九轮应用/新boot/镜像/结果见 `docs/P21_SAGIT_UNATTENDED_20261106.md`。
已迁移完整96项HWCG、CPUdomainresume上游默认opt-in、冷reset后enableCoreBlocks顺序、
用户态实际pluginaddon发布、count6TYPE4对象、多变量真实A540compilerfixture、directShaders
与Mesa按实际active槽的SSBO count。原A6xxstib.b错误已明确并在活动候选停用。

设备实际建立P1与usercompute的BO/map/IB/fence通路，guard未越界，但所有真实shader
写入仍0/64；direct/indirect/local/global/count对照均不提供成功证据。CP_REG_TO_MEM
只证明采样的CS寄存器生效。下一内部HLSQ采集必须满足上游严格健康/idle/context条件，
不得松mask或在failedkick后启动crashdumper。

## 不适用的上游案例

- sagit核心恢复仍为工程opt-in；其callback在CP/VBIF halt内轮询GPMU域状态，本机该窗口禁止这些读取，不能原样合并。
- Linux suspend等VBIF ACK后清halt再底层suspend，不证明持续halt内上电安全。
- A510/A530 VBIF reset及A530 STAT_EVENT workaround不适用于A540。
- sagit iOS18.6–18.7 `_MTLDevice`/`_limits`布局不适用于21H461；只有逻辑可参考，需重新核对ABI。
- 上游panic直接flush UFS不移植；本机panic不能睡眠、分配或等待UFS锁。

## 已合并的动态 compute subset

新增 `package/Userland/NativeMetal540Compute.h` 适配sagit上述纯状态编码，继续本机
Encoder540/Blit540及NativeAdrenoABI，未引入RegisterProgram selector或iOS18 IOGPU ABI。
`tools/gpu-native540/export_program.py` 对固定来源、完整621word反射、ISA哈希/大小、
未支持字段和输出覆盖作拒绝式校验，生成有类型metadata和独立ISA数组。

实现动态register footprint、instrlen/constlen/sysval、instrlen≤32的cached策略、
block13 constants、block15两组16项SSBO descriptors、uniform dispatch和≤32KiB共享内存。
真实CS的memory prefix112、driver112..123、immediate128..138及实际buffer lengths96..111
已在host模型逐字验证。64KiB整倍数size仅exact-bounds程序使用硬件extent+1，真实长度不变。

7项专用测试PASS，真实三buffer PM4模型/ASan/UBSan PASS，现有SDK的arm64 iOS17.7
client+生成header编译PASS。当前严格拒绝graphics/textures/images/samplers/tile/matrix/
constant-window及非uniform尾部grid。

该路径仅userland（栈>12KiB），不可用于kernel/panic。结构校验不是IR3指令安全验证；
调用者必须验证不可变compiler-owned产物哈希、正确上传ISA、物理不别名、BO映射/持有/退休。
本机core/halt仍阻塞，未把任意编译shader提交设备，未发布系统Metaldevice。

## 上游电源恢复案例的进一步核对

真实sagit `AdrenoSubmit.h::idlePowerTransition` 同时halt CP/VBIF，再要求
haltedPipelineIdle；bit30仍必须为0，恢复仍标工程opt-in。它的callback在halt内
轮询GPMU，与本板曾有总线停顿的安全约束不符，故未原样合并。
`QcomGPUHardware.cpp:1303–1318`、`AdrenoStartup.h:42–49` 是核对入口。

Linux固定源的 `a5xx_pm_suspend` 在VBIF ACK后清halt再suspend；`a5xx_pm_resume`
属于完整底层resume；vendor regulator_enable属于clocks已开启的restart。它们不提供
“只停CP、保留现环、持续VBIF halt内上电”的已验证本板案例。已有WFI/WAIT_MEM_WRITES/
DISABLE_ALL_GROUPS不能被当成bit30特定修复。未找到适用解法的项目保留门禁，不合并猜测。

旧shader文档中“认证必然重置域”和“Android认证后必然调用pm_resume”已更正：
实际源只支持观测时域关闭及secure调用路径不同，不能确定因果。

## 来源与证据

核验过的Linux/vendor源码及sources.json保存在
`out/p21/p21-native540-log-20261105T153209Z/logs/analysis-corrected-20261105T155805Z/`，
三份文件SHA256与清单完全匹配。真实sagit checkout在指定upstream路径，未修改。
日志与两个设备冻结见HANDOFF顶部；编译紧凑证据在 `tools/gpu-native540/evidence/`。

本记录中的未合并项是硬件/版本前提不满足，而非允许绕过安全门禁。所有设备结果以本机boot ID和
镜像输入哈希为准；上游代码存在或宿主测试通过不替代实机验证。
