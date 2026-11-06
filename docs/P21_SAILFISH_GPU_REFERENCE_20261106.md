# P21 · ios-sailfish GPU 上游参考（2026-11-06）

后续本机实机已确认SSBO WIDTH宏修复使64/64、256/256写入成功，默认power-collapse
也通过；详 `docs/P21_A540_SSBO_WIDTH_20261106.md`。本文调研时源码中的WIDTH修改
已存在但尚未启动验证，因此下文“本机已匹配”“未找到充分案例”是当时的只读观察，
不代表移植遗漏从未存在，也不替代后续实际设备证据。

## 范围、来源与工作边界

本次受明确授权仅拉取公开参考、只读 GPU 调研并写本文件。未修改项目代码、HANDOFF、设备、候选、启动参数或任何分区；未提交或推送；未安装依赖、运行上游构建或设备脚本。开始前读取本项目 AGENTS.md 和 HANDOFF.md 顶部，检查 Git 工作树并保留全部其他 agent 改动。

- 拉取时间：2026-11-06 03:39 UTC 起。
- 原先目标目录不存在，执行普通 `git clone --depth 1`，**没有** `--recurse-submodules`。
- 工作副本：`/Volumes/NAS-MacData/Project/IOS/upstream/ios-sailfish`。
- origin（fetch/push）：`https://github.com/joranv1/ios-sailfish`。
- 精确 HEAD：**`6f27e41ff249f2680540ebe8ee5ab982c5e641c9`**。
- HEAD 作者/提交时间：2026-10-22 21:12:18 +0200；提交标题 `README: Wi-Fi works (Settings join, DHCP, Safari), interrupt-driven touch, CPU boost option, Wi-Fi firmware blobs and boot-arg length limit`。
- 最终检查参考库 `git status --short` 为空；没有被追踪的 `.gitmodules`，`git submodule status` 为空。浅克隆没有早期提交历史，以下引用是 **此 HEAD 内的文件/补丁内容**，不是已查证的各补丁首次提交 SHA。
- 固定网页源码前缀：`https://github.com/joranv1/ios-sailfish/blob/6f27e41ff249f2680540ebe8ee5ab982c5e641c9/`。

本机设备状态采用开始时 HANDOFF 的记录：root Recovery `3b25cc7c`，最新日志目录 `/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/out/p21/persistent-logs/20261105T193115Z/`。只读其 retrieval.json 确认 boot `0x3ddd47c0`、窗口 SHA256 `9fc660049d3b764bdbf32ca256685fb1a34f2d62b58bd9345dc4e9d023f256cf`、35 rounds、0 write_errors。本次没有新连接或观测设备，因此不把交接状态称为本次实时实机验证。HANDOFF 记录最新 A540 shader 输出仍 0/64、guard 960/960；已有 fence/CP 状态不是 shader 成功。

## 一、最重要的结论

**sailfish 有真实 GPU 命令、shader、用户态驱动与系统合成接入源码，不只是 CPU framebuffer；但没有 Metal 实现。** 它走的是：

```text
backboardd / CoreAnimation 的 GLES renderer
  -> EAGLContext 私有 SPI + GLI dispatch shim（sweetgl.dylib）
  -> Mesa GLES / state tracker / Gallium freedreno A5xx / IR3
  -> sweet_drm.c 模拟 libdrm/msm ioctls
  -> SweetGPUUserClient：BO / IOSurface import / Submit / Wait
  -> sweet_gpu.c：SMMU GPU VA + CP indirect buffers + CACHE_FLUSH_TS fence
  -> GPU shader 渲染 IOSurface
  -> AppleCLCD GPU compute textured quad 缩放至双缓冲
  -> MDP 源地址切换 / panel scanout
```

这是**源码链路已确认**；README 所称 A530 GPU 加速、20–40 UI fps、最高 60 Hz 是**作者报告**，本次没下载设备日志、没有在 Pixel 或 OnePlus 5 复现实测，不能升级为本次硬件成功证明。

- `README.md:3–5,28,43`：Pixel 1 sailfish、Snapdragon 821、Adreno 530、XNU 8796、iOS 16.7.16 / 20H392 / iPhone 8 userland，明确 **No Metal**。
- `xnu/patches/0119-sailfish-gpu-power-probe.patch:33`：代码目标 Pixel 1 / MSM8996 / A530。
- `xnu/patches/0150-sailfish-gpu-bo-submit.patch::sweet_gpu_get_info`（448）：报告 chip ID `0x05030002`，注释 **A530 v2 (MSM8996 Pro)**；`tools/sweetgl/sweet_drm.c::get_param`（180–181）报告 GPU_ID 530；`tools/ir3asm/main.c:28–44` 固定 GPUId(530)、gen5。
- 上述 chip ID 是驱动常量，不是本次从真实硬件读出。源码/README一致指向 **MSM8996 Pro / Snapdragon 821 / A530**，不能当 MSM8998/A540 案例，更不能称 A6xx/Turnip。

## 二、实际实现与调用者

### 1. 驱动初始化、固件与安全前提

实际内核代码以 `xnu/patches/` 中按顺序应用的 unified diffs 保存，并非已经展开的 `bsd/dev/sweet_gpu.c` 工作树。引用原内核函数需一起引用补丁序号和后续修正。

- `0119-sailfish-gpu-power-probe.patch`：新增 `sweet_gpu_init` 并在启动路径调用，GPU电源/时钟 probe。
- `0120-sailfish-gpu-cp-test.patch::cp_test`（183–224）：A530 CP/RBBM/VBIF 配置、vendor PM4/PFP 地址、ring、CP_ME_INIT、CP_MEM_WRITE，启动 ME、更新 WPTR。
- `0121-sailfish-gpu-smmu-identity.patch::smmu_identity`：36-bit、4 KiB granule、CB0，CPU内核页仍16 KiB。
- **`0123-sailfish-gpu-smmu-stream0.patch` 后续修正**：不再 wildcard 所有流，只匹配 Pixel 的 gfx3d_user stream0；注释说 wildcard 触及 TZ secure stream 曾卡住 CPU。这不能直接搬到 cheeseburger 的 stream 配置。
- `0127-sailfish-gpu-rbccu-sp-power.patch`：独立 RBCCU/SP core domains。
- `0130-sailfish-gpu-zap-shader.patch::zap_load`（123–146）：PAS13 init_image -> mem_setup -> copy split segment -> auth_and_reset；`cp_test` 已有正常 CP_MEM_WRITE 后，再 auth 并发送 CP_SET_SECURE_MODE0。失败保持 secure，不做后续 blit。
- `0125-sailfish-gpu-no-trust-write.patch`、`0130` 必须与历史 `0129` 的失败实验区分；不要搬旧 unsecure 尝试或借此关闭本机 ZAP/PAS 门禁。
- 固件 A530 PM4/PFP/MDT/.b02 来自作者手机 vendor，不在仓库（README:71）。ZAP 固定 carveout、MMIO 地址、RPM votes、频率和A530 workaround 都不适合直接复制。

### 2. 最小 compute 与真实 shader 编译/汇编

`0131-sailfish-gpu-compute.patch::compute_test`（72–184）由 `cp_test(level>=4)` 调用，分配shader/SSBO、设置SP/HLSQ、CP_LOAD_STATE4、CP_EXEC_CS，然后CPU核对1024个输出值 `i ^ 0x5a5a0000`。这是实际shader提交/检查实现，不是打印成功就算通过。

**重要的后续失败修正**：`0132-sailfish-gpu-compute-offset-hi.patch` 增加 `mov.u32u32 r2.y,0`。`tools/gpu-shaders/fill.asm:3–19` 明确 `stgb` 的最后一个源寄存器代表 **64-bit byte offset pair r2.xy**；低32-bit偏移之外，高寄存器不能留垃圾。源里 `@invocationid(r0.x)` 是 localID；通过 workgroupID `r48.x << 6` 合成 globalID，不把localID当globalID。

- `tools/ir3asm/main.c::main`：`ir3_parse_asm` -> shader variant binary/info -> C `sweet_kernel` + `ir3_isa_disasm(.gpu_id=530)`。保留 instrlen、constlen、寄存器footprint、threadsize、branchstack、local size及sysval分配，**不是 Metal/AIR 编译器**。
- `0133-sailfish-gpu-blend-kernel.patch::compute_run`：把fill专用状态推广到 `sweet_kernel` 与多个SSBO，后续blend/sample/quad复用。
- `0136-sailfish-gpu-textured-quad.patch::compute_run`：增加 block13常量、block5 sampler/texture和二维grid；`sample_test` 与实际 `sweet_gpu_quad` 路线用于 affine / bilinear / opacity / source-over。
- 运行期系统着色器：`tools/sweetgl/eagl.c::sgl_ShaderSourceARB`（473–535）改写CA GLSL兼容性后 `glShaderSource`；`sgl_CompileShaderARB`（1167–1181）调用Mesa `glCompileShader` 并查编译状态。编译后端来自外部Mesa静态库，不是仓库内另一个私有Metalcompiler。
- `tools/sweetgl/build.sh:14–24,47–50` 实际链接 Gallium freedreno、IR3 encode/decode、GLSL、NIR及 Mesa state tracker。`mesa-setup.sh:10–18` 配置 `gallium-drivers=freedreno`、`freedreno-kmds=msm`、**Vulkan disabled**，EGL/GLX/GBM window systems disabled，但使用surfaceless frontend/EGLImage式导入。

### 3. Mesa -> kernel Submit / Memory / Lifetime

- `tools/sweetgl/sweetgl.c::sweetgl_init`（90–126）：`sweet_drm_open` -> `fd_drm_screen_create_renderonly`，`sweetgl_create` -> `st_api_create_context(API_OPENGLES2, GLES3.1)`，`sweetgl_make_current` -> `st_api_make_current`。
- `tools/sweetgl/sweet_drm.c::connect_dev`（55–75）：找 `SweetGPU`、`IOServiceOpen`、selector15查询；不是Linux DRM设备节点，fd是 `/dev/null` 的dup。
- `gem_new` selector10返回 handle/IOVA/CPU map；`gem_info` softpin/GET_IOVA；`sweet_drm_import_iosurface` selector12将IOSurface引入；`gem_submit`（299–367）把Mesa CMD_BUF转换成 `{iova_lo,iova_hi,dwords,0}` IB列表及BO handles，selector13提交；`fence_wait`/CPU_PREP使用selector14/16。
- `0150-sailfish-gpu-bo-submit.patch::SweetGPUUserClient::submit`（821起）：结构精确长度/数量检查、BO handle检查、调用 `sweet_gpu_submit`，记每个BO last_seq。
- `sweet_gpu_submit`（354–398）：核对IB对齐/范围、ring空间、CP_INDIRECT_BUFFER_PFE、CACHE_FLUSH_TS写seq、cache clean、WPTR推进；`sweet_gpu_wait` 有deadline，stuck时服务关闭到重启，不自动无限重试。
- `SweetGPUUserClient::freeBo`（704–724）：等待最后BO fence，失败保留映射和wired memory而不是在GPU可能恢复后释放；适合作资源retirement行为对照。
- **不能只读0150**：`0151-sailfish-gpu-bo-cached.patch` 改掉用户WC映射（作者注释其会改physmap PTE并冻结），使用cached用户映射、提交前对所有列出的BO clean/invalidate、CPU_PREP后invalidate；selector15不再给用户映射fence页，查询退到selector14。
- **安全差异**：底层 `sweet_gpu_submit` 的IB检查主要是全局 `vm_range`；上层只验证列举的本client BO handles，没有在所读路径中找到完整的IB内容解析/所有引用归属证明。不能以这些检查推出任意GPU命令安全，不能替换本机owner/hash/alias/资源持有门禁。
- **不要盲搬** `0156-sailfish-gpu-vm-pad-scratch.patch`：作者因vertex/texture越界fetch挂A530，把每BO后128 KiB映射为共享scratch（取代unmapped guard）。这是特定实验tradeoff，会遮盖越界并引入别名；不等于本机可接受的隔离/边界设计。

### 4. iOS 私有 EAGL / GLI SPI，不是 Metal SPI

`tools/sweetgl/eagl.c:2–15,45–51` 明确CA取 `getMacroContextPrivate` 返回的 `{void *rctx; void *disp[1036]}`；`m_init`（1721–1748）建立Mesa context并复制dispatch，`m_macro`（1794–1797）返回这个结构。`gen-dispatch.py` 按 `disp[i](rctx, gl_args...)` 生成wrapper，**不能把这看作公开OpenGLES标准ABI或MetalDevice接口**。

`eagl.c::sweetgl_eagl_install`（2207–2234）在dylib constructor调用中替换：

- `initWithAPI:properties:`、`API`、`setParameter:to:`、`getMacroContextPrivate`；
- `texImageIOSurface:target:internalFormat:width:height:format:type:plane:invert:`；
- `attachImage:toCoreSurface:invertedRender:`、current/setCurrentContext/dealloc。

`bind_surface`（1664–1701）kernel import -> `screen->resource_from_handle(DRM_FORMAT_MOD_LINEAR)` -> `_mesa_EGLImageTargetTexture2DOES`；有格式/stride/base offset处理。pitch非64-byte对齐会走 `copy_surface`（1559起）CPU上传texture。主GPU路径不是CPU整帧合成，但**不是任何路径绝无CPU copy**。

- `ios-dispatch.txt` 有410行已命名slot，表容量1036；generator对没有Mesa映射的slot记录unsupported并返回0。不能认为1036个调用已全部实现，迁移需为目标系统核验实际被调用slot，拒绝以静默stub制造完成。
- `sgl_install_texfns`（2178起）直接针对特定QuartzCore shared-cache布局替换shader函数表达式/表；`sgl_patch` 检查预期指令后patch。**即使同20H392也要核对具体cache/架构/UUID/hash；21H461不能沿用其地址、slot或指令指纹。** 本文不复制地址。
- `0166-sweet-ioacceleratores-iokit-quiet.patch` 注册无MetalPluginName的 `IOAcceleratorES` 以避免CAMetalContextCreate查找延迟，注释明确Metal跳过、CA继续选GLES。这个空service不是Metal设备，也不是GPU渲染能力证明。
- README称未修改Apple userland；但本源码含运行期QuartzCore/GLSL兼容改写，因此不应解释成“不对Apple运行时行为做任何hook/patch”。

### 5. 显示最终消费者

`0164-sailfish-gpu-present-600mhz.patch::AppleCLCD::gpuLayer`（299–326）构造 `sweet_gpu_quad` 从IOSurfaceGPU VA缩放到backbuffer，调用真实 `compute_run`；swap path（397、415、440–446）启用GPU、提交各layer、`sweet_fbcon_flip`并轮换buffer。`0166` 仅移除首次clear/flip诊断睡眠。GPU不可用仍有 `blitScaled` / CPU行拷贝 fallback，测试模式也可跑CPU对照。显示成功与shader成功必须分别验证。

## 三、A5xx WIDTH / SSBO / HLSQ 重点可复用核对

### SSBO 两类 descriptor，确切字段位置

`sailfish 0131:130–141 / 0133::compute_run:116–133` 的两个 `CP_LOAD_STATE4` block15包：

1. ST4_CONSTANTS(1)：NUM_UNIT=覆盖的slot数，每个buffer两word，word0 **`(bytes & 0xffff) << 16`**，word1 **`bytes >> 16`**。
2. ST4_UBO(2)：相同slot次序，每个buffer两word，IOVA低/高32-bit。

**WIDTH 在word0高16bit，不是byteSize直接放word0低16bit。** 长度单位是bytes，不能把GLSL uint元素数当bytes。这里连续 `num_bufs`，稀疏binding应按最高启用slot计span而不是popcount。

已对照本机已存在固定Mesa **`769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de` (24.3.4)**：

- `/Users/dinnerabc/unixnu-p21/native540/obj/gpu-compiler/mesa/src/freedreno/registers/adreno/a5xx.xml:3012–3018`：A5XX_SSBO_1 word0 WIDTH bits16..31；word1 HEIGHT bits0..15。
- 本机 `package/BoardRuntime/A5xxRegs.h:2008–2012,2479–2480` 同样mask/shift。
- 本机 `package/Userland/NativeMetal540Compute.h:219–231` **已经**使用正确WIDTH宏、HEIGHT宏及最高active slot span，`CheeseburgerGpuCompute540.h:133`也正确。此次未发现“本机WIDTH错放低位”可直接修复。
- sagit固定 **`673f85070fe9f7872793015eb230a5c3987d4a8c`**，origin `https://github.com/MC-XiaoXiao/ios_sagit.git`，`drivers/gpu/QcomGPU/AdrenoPipeline.h::buffers`（479–491）一致；含特定exact bounds下低dimension=0的extent+1策略。不能由sailfish推出所有64 KiB边界必须+1，本机已有对shader显式bounds的受限处理。

### HLSQ / SP allocation / state

sailfish `0131:106–149` 与 `0133::compute_run` 给出适用gen5结构：

- HLSQ_CONTROL_0_REG `(thr<<2)|0x880`；SP_CS_CTRL_REG0 threadsize、full/half footprint（max_reg+1）、branchstack及BUFFER。
- SP/HLSQ_CS_CONFIG enabled；HLSQ_CS_CNTL `(cached_instrlen<<1)|SSBO_ENABLE`；HLSQ_CS_CONSTLEN为kernel vec4 constlen/4、CS_INSTRLEN为16-instruction单位。
- shader object 地址连同private-memory共 **6个连续TYPE4 register payload word**；HLSQ_UPDATE_CNTL `0x01f00000`。
- HLSQ_CS_CNTL_0 编译产物的WGID/localID +两个unused sysval=252；CNTL_1=1；没有A6xx shared-size编码。
- block13 SS4_INDIRECT load shader；constants是同block ST4_CONSTANTS、NUM_UNIT按vec4；grid local-1/global-size/group-count，CP_EXEC_CS，然后flush/fence。

本机 `NativeMetal540Compute.h:186–237` 已有这套核心结构，并额外清VS/FS残态、cached≤32 / direct code有限准入；sagit `AdrenoPipeline.h::dispatch:925–992` 一致。固定Mesa `src/gallium/drivers/freedreno/a5xx/fd5_compute.c::cs_program_emit / fd5_launch_grid` 也核对sysval、HLSQ allocation和grid。因此这是**交叉支持已有编码的参考**，不是证明本机HLSQ内部真实装载完成。

**绝不能直接搬的A530 state**：sailfish compute restore 明写 `SP_DBG_ECO_CNTL=0x40000800 /* not A540 */`（0131:100），而sagit A540 restore `SP_DBG_ECO_CNTL=0x800`（381）。CP token-end-sync、ROQ/MERCIU、clock、电源/secure carveout另需MSM8998 vendor核对。不能为了“照抄可运行”把A530表盖到A540。

### ISA 禁区

固定Mesa `src/freedreno/isa/ir3-cat6.xml` 中 `stgb` 的a5xx继承族（662起）和 `stib.b` 的a6xx继承族（1258起）是不同编码；post-A5xx store不可裸贴给A540。sailfish使用 `stgb.untyped.4d.u32.1`，可作为gen5 source-level最小probe参考，但移到540仍应以固定backend生成/反汇编、核对resource反射和64-bit offset pairs。assembler接受gpu参数不自动证明每条指令代际合法。

## 四、适配建议（不是已应用修改）

1. **当前shader阻塞优先最小compute差异核对**：用sailfish fill的64-bit offset高位清零、local/group sysval、完整restore/SP/HLSQ、descriptor长度与缓存状态作源级对照；保持A540 vendor状态与现有安全门禁。已有compiled local/global-ID、direct/indirect试验0write不能简单重复，先获得能区分constant/实际state装载/ISA寄存器初始化的证据。
2. 本机SSBO WIDTH和基本HLSQ字段已经匹配三份来源，**不要将另一次同样移植或寄存器读回称为修复**。first failure应继续基于本机新日志定位，fence不是SSBO写成功。
3. 若用户选择短期先系统GPU UI而非Metal，可评估sailfish **GLES + Freedreno + EAGL shim** 作为单独路线：现有BO/Submit/Wait后端桥接，不引入Turnip/Vulkan ICD。仍须真实540 shader输出→三角形→IOSurface import/render→display flip逐级验证。**它不会解决Metal-only应用。** 本次未切换用户当前Metal路线。
4. 要做Metal仍沿用sagit本机已有AIR/GLSL->NIR->IR3与shader-aware bindings，以及目标OS私有Metal device/library/queue接口；sailfish不提供这些接口。它的SPI经验可复用“按目标cache核对、只在合法context安装、失配拒绝”的方法，不可复用EAGL ABI为Metal ABI。
5. 资源管理可参考last_seq retirement、hung quarantine、cached BO+CPU_PREP；不搬共享scratch padding、不放宽bounds、不借用全局IB范围检查取代client所有权、不关AMFI/sandbox/secure来使原版sweetgl注入路径工作。
6. Display只参考IOSurface lifetime/stride/format、GPU scale与double-buffer/fence次序；panel、MDP地址、24bpp转换、频率、DPU表页、物理布局必须依据OnePlus5本板，不移植Pixel地址或memory carveout。

## 五、缺证与复现限制

- 此HEAD没有完整Mesa源码/精确Mesa tag或commit pin；`mesa-setup.sh` / `build-ir3asm.sh` 实际依赖外部 `research/mesa`，未发现它们自行clone或固定SHA。README“fetched and built”不能当可复现脚本证据。Mesa patch index blob短hash也不是repo commit。本文固定Mesa24.3.4只是**本机交叉对照版本**，不声称作者使用该版。
- 没有上游Pixel原始日志/独立shader输出或fps测量，本次也没有验证作者固件/Root镜像。所有WORKS字符串仍需条件与实际日志支持。
- 没有证明EAGL 1036-slot布局及QuartzCore patches兼容本机当前21H461；相同内核版本也不足以证明用户态私有ABI相同。
- 没有MetalDevice/MetalCompiler/Apple AIR loader，也没有完整任意命令安全验证；存在明确CPU fallback/texture upload。
- 没找到用sailfish源码直接解决本机“真实A540 program/fence成立但shader SSBO0/64”的充分案例，不能据A530成功主张改WIDTH或供电即可解决。
- 额外只读检查sagit与UniXNU-sheng checkout均clean；sheng HEAD `7cff07ea9933428e27769014e903f103ab31155d`，origin `https://github.com/UniXNU/UniXNU-sheng.git`。本文具体gen5编码对照来自sagit和固定Mesa，不将sheng新代GPU命令作为A540依据。

## 六、紧凑文件 SHA256（本次所读公开来源）

所有下列路径相对于上述 sailfish 工作副本，归属同一固定 HEAD：

| 文件 | SHA256 |
|---|---|
| `xnu/patches/0131-sailfish-gpu-compute.patch` | `a6a60047a12c23805c69072dbad98307b2a7cf3c319cf30de9f0f5f12f90296a` |
| `xnu/patches/0132-sailfish-gpu-compute-offset-hi.patch` | `513bd8350b5b1ee7a0ae867fb77a1d1dcaab4c86393a3c39dc9a191ff3cf8687` |
| `xnu/patches/0133-sailfish-gpu-blend-kernel.patch` | `543403b96e4fef3cfc924067d167d5578e5c396d85dcb5c81a7680d72b411427` |
| `xnu/patches/0150-sailfish-gpu-bo-submit.patch` | `8cb6f2564d9f34e68dc57f7824908b06719abe0557cec517f4badb21cfb7b8be` |
| `tools/sweetgl/eagl.c` | `7d73f44d6713a4d82678d598caee093ace5a9d87da8608c369a90ad3c8d36169` |
| `tools/sweetgl/sweet_drm.c` | `8d0f77c50264d0a92d7a3762f1eefdceb86e67b50d286d704df96185377e4890` |

交接：主agent只需引用此文件和固定SHA；本次仅新增本文件，不编辑正在多人更新的HANDOFF，由主agent最终整合。不得运行上游root/flash/build/push脚本作为本机适配步骤。
