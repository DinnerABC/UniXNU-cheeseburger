// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540Capabilities.h"
#include <cstdlib>
#include <cstring>
// Pinned upstream QcomMTL device initializer (related method, not195 semantics):
// ios_sagit drivers/gpu/QcomMTL/QcomMTL.mm
// 673f85070fe9f7872793015eb230a5c3987d4a8c. No literal supportsX methods there;
// do not copy generated Apple profiles or extend its advertised capabilities.
// Target21H461 Metal UUID: 5e7ceec3fdea3f9a97d8a710986b45d4
// helper SHA256: 14d3611d94ae41b1baa1375820ea1b18ebd398612a64543b00c2f4ccb6c6adee
// function starts SHA256: ea4b286859eeaff44315dce9f2984da6ff993c113b3dce438b1343cfe81d5485
// query init0x187aa8c88/11524B SHA256:
// 1d1cc0e2c08d9dd641650c317b7778b904fdd6b1bd1c85de638f928dbccd13fa
// audit queries.json SHA256: 2ed5eb28c13e7a5bd646696a839500b828698d86bdaab4fd7c77ceb273c07b13
// All195 target IMPs load self+0x160 then tailcall an uninitialized feature
// object during init; NEVER invoke those inherited methods. Target neutral
// NSUIntegerMax/no Apple families branch audited, not installed by this code.
// Scope evidence: Objects newTextureWithDescriptor admission; Render linear
// MRT/pitch and AIR; HANDOFF boot0x3f177281, persistent-logs/20261106T111119Z,
// rawSHA dc30feed45890598f8f57360fe25cbf4f24f5f6ebb5b1df45a11c2419a657cc7.
// Restricted64RGBA8 raster is demonstrated; NOT presentation/general renderer.
// Compute constants[96+i]/AIR guards do not prove general bounds-checking, and
// AIR copy does not prove the standard read/write argument contract: both NO.
namespace NativeMetal540Capabilities {
namespace {
constexpr uint32_t Required = LinearRGBA8RasterCode | LinearRGBA8DevicePixels |
    RestrictedDescriptorAdmission;
static const Entry Policy[] = {
    {"supportsBufferlessClientStorageTexture", "B16@0:8", "0x187b535d0", "acc5a36aea2be6b11e39adafc59fdd187d4022eda4b1d7a1a28efb24bc8f1ade", "Current admitted native backend does not provide Bufferless Client Storage Texture; not a hardware inability claim.", false, 0},
    {"supportsComputeMemoryBarrier", "B16@0:8", "0x187aabfa8", "fb05c0761991562173ca11326d64ca7e01569efef8589f2b95c4b4ccde6192d1", "Current admitted native backend does not provide Compute Memory Barrier; not a hardware inability claim.", false, 0},
    {"supportsRenderMemoryBarrier", "B16@0:8", "0x187aabd50", "cea58b0dc77c523daeae82b282c608659538707ead6caf06c89305f4034e64df", "Current admitted native backend does not provide Render Memory Barrier; not a hardware inability claim.", false, 0},
    {"supportsPartialRenderMemoryBarrier", "B16@0:8", "0x187b535d8", "6eb69375def3269dd500ab10aedc3683509c58e53beba0d820b549f6da521145", "Current admitted native backend does not provide Partial Render Memory Barrier; not a hardware inability claim.", false, 0},
    {"supportsArgumentBuffersTier2", "B16@0:8", "0x187aac000", "f3e29ce9c3adef0d5d3373d5c8856b9a369b487bc86d2961b9b3605f89c83ad2", "Current admitted native backend does not provide Argument Buffers Tier2; not a hardware inability claim.", false, 0},
    {"supportsReadWriteTextureArgumentsTier2", "B16@0:8", "0x187aabbf8", "9e6021d04342b5a5bb31f0805c91ae66f15c1bc656486d7344cc5e80afda0c41", "Current admitted native backend does not provide Read Write Texture Arguments Tier2; not a hardware inability claim.", false, 0},
    {"supportsStreamingCodecSignaling", "B16@0:8", "0x187b535e0", "79a6c9b36db75756dd1e09fef1a09b69b018f4f33b2a2ca76f13afe5515f144d", "Current admitted native backend does not provide Streaming Codec Signaling; not a hardware inability claim.", false, 0},
    {"supportsProgrammableSamplePositions", "B16@0:8", "0x187aabe08", "e86c443113511e861b4c7cf7ddde6b611be7d59a23574f828538822c2bdad077", "Current admitted native backend does not provide Programmable Sample Positions; not a hardware inability claim.", false, 0},
    {"supportsLargeFramebufferConfigs", "B16@0:8", "0x187b535e8", "f4d00ff11d221893d2ff24a032eecb25ac1f5a9d67a06a772cd37cbfc0433a47", "Current admitted native backend does not provide Large Framebuffer Configs; not a hardware inability claim.", false, 0},
    {"supportsCustomBorderColor", "B16@0:8", "0x187b535f0", "323ea89f90bfcd11c576fb07b4eb8162199f843ccbc04915dd43570029ede32a", "Current admitted native backend does not provide Custom Border Color; not a hardware inability claim.", false, 0},
    {"supportsSamplerAddressModeClampToHalfBorder", "B16@0:8", "0x187b535f8", "60221172457d6218667f5b93caea45598d17200757006590534e6f863658aa92", "Current admitted native backend does not provide Sampler Address Mode Clamp To Half Border; not a hardware inability claim.", false, 0},
    {"supportsBCTextureCompression", "B16@0:8", "0x187b53600", "4a8bfc283cfbd3ccf64f33e08dab21d5ab7e678633e2842d76f083072937fe20", "Current admitted native backend does not provide BCTexture Compression; not a hardware inability claim.", false, 0},
    {"supports3DBCTextures", "B16@0:8", "0x187aac028", "cbcd80dcd0de6d46d38b666453999f8cbf5cd7638e1c3d5e716c7cbf726c5e44", "Current admitted native backend does not provide 3 DBCTextures; not a hardware inability claim.", false, 0},
    {"supportsRGBA10A2Gamma", "B16@0:8", "0x187aabde0", "7ca8663a31a02ffa4f87c0ed2693a1a641adeb6eeabd9278656ce2c08bded532", "Current admitted native backend does not provide RGBA10 A2 Gamma; not a hardware inability claim.", false, 0},
    {"supportsBGR10A2", "B16@0:8", "0x187b53608", "424f4aa7d296a8a4eb774e08a3421b4b989a90e94cf04e55c8bbfbf3f337832c", "Current admitted native backend does not provide BGR10 A2; not a hardware inability claim.", false, 0},
    {"supportsPrimitiveRestartOverride", "B16@0:8", "0x187aabe10", "866c2cfc88e59db3dfb711a5295f1f16bf5410c328acd8f8d851b751e6ab7a87", "Current admitted native backend does not provide Primitive Restart Override; not a hardware inability claim.", false, 0},
    {"supportsGlobalVariableRelocation", "B16@0:8", "0x187b53610", "65473b454951f413c01c5f0d7561598573be1064a1252080a22702fca5a6c5ef", "Current admitted native backend does not provide Global Variable Relocation; not a hardware inability claim.", false, 0},
    {"supportsGlobalVariableRelocationRender", "B16@0:8", "0x187b53618", "42ed8b401cd087ae11010df4ce35e6dcb0f64037c34d931f3d3716839d6dec38", "Current admitted native backend does not provide Global Variable Relocation Render; not a hardware inability claim.", false, 0},
    {"supportsGlobalVariableRelocationCompute", "B16@0:8", "0x187aab994", "e7f143719db39f8c48aa9822fdcfd59bb5b5f29c3db639319f72500b7f05f276", "Current admitted native backend does not provide Global Variable Relocation Compute; not a hardware inability claim.", false, 0},
    {"supportsTLS", "B16@0:8", "0x187b53620", "408bb891cad30f2d3bc044b666082a4e409c7540b48c597356bc3096aa6b8013", "Current admitted native backend does not provide TLS; not a hardware inability claim.", false, 0},
    {"supports32bpcMSAATextures", "B16@0:8", "0x187b53628", "799ab2e3f9a02e36097e2051574f98034b7497975bd59eb1038c72367f6425f3", "Current admitted native backend does not provide 32bpc MSAATextures; not a hardware inability claim.", false, 0},
    {"supports32BitMSAA", "B16@0:8", "0x187aac038", "dbeea2898044ac15904d064023aa3e3d56f1eab9359f171508bc3786c28c792b", "Current admitted native backend does not provide 32 Bit MSAA; not a hardware inability claim.", false, 0},
    {"supports32BitFloatFiltering", "B16@0:8", "0x187aabc38", "441d2cdbc73070e829ab9204cabbbc60cd5f75acdad9d82e7433ab0af17fc4ae", "Current admitted native backend does not provide 32 Bit Float Filtering; not a hardware inability claim.", false, 0},
    {"supportsQueryTextureLOD", "B16@0:8", "0x187b53630", "642241eb300595b5cedf9654313ac273eb9cb52b7466e38c0b81ac45e5f8e2d6", "Current admitted native backend does not provide Query Texture LOD; not a hardware inability claim.", false, 0},
    {"supportsVertexAmplification", "B16@0:8", "0x187b53638", "e1e0c65ea35c00e73f07d10d5ae8a1df01534d8148b8c61d9be3c9e7aebd6f0b", "Current admitted native backend does not provide Vertex Amplification; not a hardware inability claim.", false, 0},
    {"supportsPlacementHeaps", "B16@0:8", "0x187aabe28", "2321f438a4b9fc5c2e43125d6244a3b23a3e34a7a38ed1bc4f5d07548f3e52b0", "Current admitted native backend does not provide Placement Heaps; not a hardware inability claim.", false, 0},
    {"supportsOpenCLTextureWriteSwizzles", "B16@0:8", "0x187aabe48", "35c9f1215d6e5edd8d6024e283d72f0f252c9147480ede5a8a5dfdb4d31e7506", "Current admitted native backend does not provide Open CLTexture Write Swizzles; not a hardware inability claim.", false, 0},
    {"supportsPullModelInterpolation", "B16@0:8", "0x187aabdf8", "54b639dafad6c0cd59cda89e389a630d1590ea7bb1d5d8760f8efb8c78a75b83", "Current admitted native backend does not provide Pull Model Interpolation; not a hardware inability claim.", false, 0},
    {"supportsInt64", "B16@0:8", "0x187aabeb0", "e74d52c0bda76cedb1cf35b0c13771f5d99ae7f6308539d3bb0a02fcb320545b", "Current admitted native backend does not provide Int64; not a hardware inability claim.", false, 0},
    {"supportsFixedLinePointFillDepthGradient", "B16@0:8", "0x187aabf48", "5700a66cf91a3d7f109cb04a36f53f243a7e55624a9a788cd4bb5fa72590cde0", "Current admitted native backend does not provide Fixed Line Point Fill Depth Gradient; not a hardware inability claim.", false, 0},
    {"supportsLateEvalEvent", "B16@0:8", "0x187b53640", "85c995ca63ddc2f0e166bae23e7fa270b4acb5fcae344ae1f8177771b81d1d00", "Current admitted native backend does not provide Late Eval Event; not a hardware inability claim.", false, 0},
    {"supportsNonZeroTextureWriteLOD", "B16@0:8", "0x187aabe50", "95328d438eff552db2910b1508fdf8684fc32c9e1bdc36030ab25719edffb921", "Current admitted native backend does not provide Non Zero Texture Write LOD; not a hardware inability claim.", false, 0},
    {"supportsSharedTextureHandles", "B16@0:8", "0x187aabcf0", "6c530af9384a86c0f472c894484242e2fc6832ca7e5cdfdffc850985eef5d8ab", "Current admitted native backend does not provide Shared Texture Handles; not a hardware inability claim.", false, 0},
    {"supportsBufferPrefetchStatistics", "B16@0:8", "0x187aabfd8", "fabb66e0a3ba6dbbdf90b442320293d750e2628844ba5533999247693ffbf1f4", "Current admitted native backend does not provide Buffer Prefetch Statistics; not a hardware inability claim.", false, 0},
    {"supportsLimitedYUVFormats", "B16@0:8", "0x187aabe98", "3e2ff9f16c8779e7641fd848d9e7aa3893b505d5d3b099bdb51ff7a735b9c350", "Current admitted native backend does not provide Limited YUVFormats; not a hardware inability claim.", false, 0},
    {"supportsNonPrivateDepthStencilTextures", "B16@0:8", "0x187a82038", "c1ad6fd628c37ad116987ef3335e2a56dafdb810834b8716bf52bd06b4881423", "Current admitted native backend does not provide Non Private Depth Stencil Textures; not a hardware inability claim.", false, 0},
    {"supportsNonPrivateMSAATextures", "B16@0:8", "0x187a82030", "504825cf1d6764f0ac620350d44b578314b5dee1cbd477cceeff5ab9a96dfd6c", "Current admitted native backend does not provide Non Private MSAATextures; not a hardware inability claim.", false, 0},
    {"supportsSharedStorageHeapResources", "B16@0:8", "0x187aabb74", "a626c8254ca80533f9b00e99f4d77842ba6b5b395700361328e81c0f84677d34", "Current admitted native backend does not provide Shared Storage Heap Resources; not a hardware inability claim.", false, 0},
    {"supportsSharedStorageTextures", "B16@0:8", "0x187aabcf8", "f87fc41ad2a25041075e87ae4e7a0af54e8668fdd1bf776866cc2418da20114b", "Current admitted native backend does not provide Shared Storage Textures; not a hardware inability claim.", false, 0},
    {"supportsLinearTextureFromSharedBuffer", "B16@0:8", "0x187aabe88", "833dbbdd12513d8782b44fb29e206f251e76af8c6a6377d28326c7febf25c635", "Current admitted native backend does not provide Linear Texture From Shared Buffer; not a hardware inability claim.", false, 0},
    {"supportsPipelineLibraries", "B16@0:8", "0x187aabe30", "97e747bb05ce5c360c01fd5c8017d0c4bf0988ae18d32396f9917406350cae31", "Current admitted native backend does not provide Pipeline Libraries; not a hardware inability claim.", false, 0},
    {"supportsFragmentOnlyEncoders", "B16@0:8", "0x187b53648", "127f3af735127b5698716328323123810b761bc1aedf015a2d7f690660b295ec", "Current admitted native backend does not provide Fragment Only Encoders; not a hardware inability claim.", false, 0},
    {"supportsBufferWithIOSurface", "B16@0:8", "0x187aabfd0", "a554b1546edc23917029966e35d4e57ff7c4b358bedf325d992bf503e9344cf4", "Current admitted native backend does not provide Buffer With IOSurface; not a hardware inability claim.", false, 0},
    {"supportsProgrammableBlending", "B16@0:8", "0x187aabba8", "de5c46b6819a098f00b5a63b47852a76d1f1ca42c1e7d41ea51e971672d3acd7", "Current admitted native backend does not provide Programmable Blending; not a hardware inability claim.", false, 0},
    {"supportsRenderToLinearTextures", "B16@0:8", "0x187aabd38", "a0435dfaddac63c3305361290bb4446df79c9106e5d79c160724f05d780d4a08", "Admitted implementation only: 64x64 single-sample RGBA8Unorm linear/sysmem raster, one attachment and fixed AIR VS/FS; Objects descriptor admission rejects other formats, sizes, MSAA, depth, resolve and sampling. Device RG status4/read1/bad0, boot3f177281.", true, Required},
    {"supportsMemorylessRenderTargets", "B16@0:8", "0x187b53650", "b432e943cb4113c8b48f27b86fca33916478c863b8bce59677731c353c1a4c4c", "Current admitted native backend does not provide Memoryless Render Targets; not a hardware inability claim.", false, 0},
    {"supportsFastMathInfNaNPropagation", "B16@0:8", "0x187aabf58", "09eab13e14f9a64ad608176b7ca9c5debba6508b9197af2860066f1eaaa52d78", "Current admitted native backend does not provide Fast Math Inf Na NPropagation; not a hardware inability claim.", false, 0},
    {"supportsInvariantVertexPosition", "B16@0:8", "0x187aabea0", "b97594521709356896dc6620625bed18163547139ae60b618f29b3224c69a59f", "Current admitted native backend does not provide Invariant Vertex Position; not a hardware inability claim.", false, 0},
    {"supportsShaderLODAverage", "B16@0:8", "0x187aabd08", "3a121815a49324858343645409da54f57e0293088e18f5338b5bc2fa99d9c443", "Current admitted native backend does not provide Shader LODAverage; not a hardware inability claim.", false, 0},
    {"supportsRelaxedTextureViewRequirements", "B16@0:8", "0x187aabd60", "00ebde69f2b067565d71299b4fad1dff8fed3cfeb2f66ef21ca06c0faf4f6f0c", "Current admitted native backend does not provide Relaxed Texture View Requirements; not a hardware inability claim.", false, 0},
    {"supportsSeparateDepthStencil", "B16@0:8", "0x187abd31c", "dfd0f61105d564f50386ed84e5251665d12016e21e93a5a1aded31c699ce94ce", "Current admitted native backend does not provide Separate Depth Stencil; not a hardware inability claim.", false, 0},
    {"supportsGPUStatistics", "B16@0:8", "0x187aabf00", "93b408009e2e38793892d4386dd48ed60f273aba5f2cb0b860278670098319a9", "Current admitted native backend does not provide GPUStatistics; not a hardware inability claim.", false, 0},
    {"supportsCompressedTextureViewSPI", "B16@0:8", "0x187aabfb0", "3c19e55f9842ba104bf4d241839008c1257a6cc8df3643be022d61ae8ba71b6f", "Current admitted native backend does not provide Compressed Texture View SPI; not a hardware inability claim.", false, 0},
    {"supportsRenderTargetTextureRotation", "B16@0:8", "0x187aabd48", "736e4b30596ed3ff734f7021cc29f9d6a30cae4d9392f94c3a30358c1ed7456f", "Current admitted native backend does not provide Render Target Texture Rotation; not a hardware inability claim.", false, 0},
    {"supportsDynamicControlPointCount", "B16@0:8", "0x187aabf78", "640ade53368d7fc5061f27ae3493192a906539d650607b1dfabd24ea3a3948da", "Current admitted native backend does not provide Dynamic Control Point Count; not a hardware inability claim.", false, 0},
    {"supportsIABHashForTools", "B16@0:8", "0x187aabef0", "49faa199f9b4ce29424360fb423785e60e2f4974e8064ace233d479c913ca667", "Current admitted native backend does not provide IABHash For Tools; not a hardware inability claim.", false, 0},
    {"supportsBinaryArchives", "B16@0:8", "0x187aabc40", "bbc20d7d8bf7ed14c5a24aeadc003843a8579889d6dc878543af24788a9c0a8c", "Current admitted native backend does not provide Binary Archives; not a hardware inability claim.", false, 0},
    {"supportsBinaryLibraries", "B16@0:8", "0x187aabfe0", "d102f4a291102d7dc0d85d45eb76e3871cba766cbf7bf97e69181de28328c809", "Current admitted native backend does not provide Binary Libraries; not a hardware inability claim.", false, 0},
    {"supportsDeadlineProfile", "B16@0:8", "0x187b53658", "6158e5e968967ad4419acf6ca53812f8f6fc775b8155b14f86201b1b5330fc43", "Current admitted native backend does not provide Deadline Profile; not a hardware inability claim.", false, 0},
    {"supportsFillTexture", "B16@0:8", "0x187aabf50", "fe26b835d60bb44bb09d8187e1398fa1392c137babbaeeb5910c4df2cd2ddb16", "Current admitted native backend does not provide Fill Texture; not a hardware inability claim.", false, 0},
    {"supportsSetThreadgroupPackingDisabled", "B16@0:8", "0x187aabd10", "469011e45d039bb2ae90b56afda82f7e2ce32db6be82b3a9661d3bd90868b2a7", "Current admitted native backend does not provide Set Threadgroup Packing Disabled; not a hardware inability claim.", false, 0},
    {"supportsASTCTextureCompression", "B16@0:8", "0x187aac018", "39e8e24caeb582a9377b2cce0f0606089f26d1645b91c97e42c5c453595e593f", "Current admitted native backend does not provide ASTCTexture Compression; not a hardware inability claim.", false, 0},
    {"supportsExtendedYUVFormats", "B16@0:8", "0x187aabf68", "299d99a13d434ad51f02bceaaac0ba24cc76d1a5483b2f68f7a1e4bfd34be9cd", "Current admitted native backend does not provide Extended YUVFormats; not a hardware inability claim.", false, 0},
    {"supportsPublicXR10Formats", "B16@0:8", "0x187aabe00", "b4588e0c7374fcdddf57bed7f0afd60539e4496d9ff9ab4704649ea418668339", "Current admitted native backend does not provide Public XR10 Formats; not a hardware inability claim.", false, 0},
    {"supportsSRGBwrites", "B16@0:8", "0x187aabd20", "5f58a23f443880c964a909a6c23588abc6d82c2502724e51499b49f46decc2c4", "Current admitted native backend does not provide SRGBwrites; not a hardware inability claim.", false, 0},
    {"supportsDepthClipMode", "B16@0:8", "0x187aabbd0", "ee9894454dd1bbd02c5e5ebc0dba4a73212f048f12f1cc7979c45cacce8ecebf", "Current admitted native backend does not provide Depth Clip Mode; not a hardware inability claim.", false, 0},
    {"supportsResourceHeaps", "B16@0:8", "0x187aabd30", "d6918f7bdc40f49b68cbf4113d2310192303779f6a02e46f27dec4abb160eedd", "Current admitted native backend does not provide Resource Heaps; not a hardware inability claim.", false, 0},
    {"supportsArgumentBuffers", "B16@0:8", "0x187aabff8", "cd8952a82c58cda06fde1d3606563132cdcf8ceb6d7fe56406e95ef23e1b3379", "Current admitted native backend does not provide Argument Buffers; not a hardware inability claim.", false, 0},
    {"supportsPacked32TextureBufferWrites", "B16@0:8", "0x187aabe40", "5e28b1d53e11009b5ceac53c52ff8c398f7e00d44872d8c205afcb77fa31b2de", "Current admitted native backend does not provide Packed32 Texture Buffer Writes; not a hardware inability claim.", false, 0},
    {"supports3DASTCTextures", "B16@0:8", "0x187aac030", "2952d0e017cd3226e08cc4708421af41a6fe7f86496aa3f66d5879c2e9116896", "Current admitted native backend does not provide 3 DASTCTextures; not a hardware inability claim.", false, 0},
    {"supportsExtendedXR10Formats", "B16@0:8", "0x187aab98c", "138a5ab21edcdbc8ea59552a7ba3e34e928bc0863816e3d9f76ae0ca0758ee3c", "Current admitted native backend does not provide Extended XR10 Formats; not a hardware inability claim.", false, 0},
    {"supportsFragmentBufferWrites", "B16@0:8", "0x187aabf28", "fb08ebbc9ed413eb2eed5c501220c6b31128e3c86ee479805293c5b74998f83d", "Current admitted native backend does not provide Fragment Buffer Writes; not a hardware inability claim.", false, 0},
    {"supportsCountingOcclusionQuery", "B16@0:8", "0x187aabf98", "d94af5d46f565d2234ed7cc108e4875a5c2daceee390b3565df33aee10124548", "Current admitted native backend does not provide Counting Occlusion Query; not a hardware inability claim.", false, 0},
    {"supportsBaseVertexInstanceDrawing", "B16@0:8", "0x187aabbd8", "26c3e10f16035c59b5414e9a42b33068a8e91467bd787b5003f6fbbbc111feb2", "Current admitted native backend does not provide Base Vertex Instance Drawing; not a hardware inability claim.", false, 0},
    {"supportsIndirectDrawAndDispatch", "B16@0:8", "0x187aabed8", "7e98722e1beb36035f0f3c5304dfdf43487889f8c4e0a7a61f95c3fbdd474c04", "Current admitted native backend does not provide Indirect Draw And Dispatch; not a hardware inability claim.", false, 0},
    {"supportsTessellation", "B16@0:8", "0x187aabb98", "9683879de6b1d1d88ec4317e0377c06b107d201beb5d0b4e0297a9f93c797c35", "Current admitted native backend does not provide Tessellation; not a hardware inability claim.", false, 0},
    {"supportsReadWriteBufferArguments", "B16@0:8", "0x187aabd70", "026ed48a8e35876824e084698a0bcd4701762301651ffa3d0cf31bfc3602f128", "Current standard read/write buffer-argument contract is not provided: fixed AIR padding_gradient copy and controlled three bindings do not establish full standard buffers/protocol support.", false, 0},
    {"supportsArrayOfTextures", "B16@0:8", "0x187aabc30", "21618debbd7f0337adc74e1422098d5d9ac0e4e303ab5ac10bb9c03eb33edc1b", "Current admitted native backend does not provide Array Of Textures; not a hardware inability claim.", false, 0},
    {"supportsArrayOfSamplers", "B16@0:8", "0x187aabff0", "2d090bef3226d7e41523a8aa30b3dd64f604b7fcb7f928852a864343c958509a", "Current admitted native backend does not provide Array Of Samplers; not a hardware inability claim.", false, 0},
    {"supportsCombinedMSAAStoreAndResolveAction", "B16@0:8", "0x187aabfb8", "10c3a9e977ead9dde123d784b64088feeb1f0da7791b0f46497263f9be7513da", "Current admitted native backend does not provide Combined MSAAStore And Resolve Action; not a hardware inability claim.", false, 0},
    {"supportsMutableTier1ArgumentBuffers", "B16@0:8", "0x187aabe60", "0d7dd98fad063e7a6560364df61b184eee87454cc68b959c83be7f37311b67b6", "Current admitted native backend does not provide Mutable Tier1 Argument Buffers; not a hardware inability claim.", false, 0},
    {"supportsSamplerCompareFunction", "B16@0:8", "0x187aabd18", "8cac5f05801136cdce926a08ca7b334c963d3ddca278d0f4b991aeee29834cb4", "Current admitted native backend does not provide Sampler Compare Function; not a hardware inability claim.", false, 0},
    {"supportsMSAADepthResolve", "B16@0:8", "0x187aabbc0", "d8f94ae9d1f4f759389fe1a71fbe3f71e47ccc6988d813386a2019bfd9d6b9d3", "Current admitted native backend does not provide MSAADepth Resolve; not a hardware inability claim.", false, 0},
    {"supportsMSAAStencilResolve", "B16@0:8", "0x187aabbb8", "bd14b17e4fabfe262e6d3e5d2cc1f759be863e695c8a0b0cc79dbb1481340fd0", "Current admitted native backend does not provide MSAAStencil Resolve; not a hardware inability claim.", false, 0},
    {"supportsMSAADepthResolveFilter", "B16@0:8", "0x187aabe80", "6defef5b390b8b50d3e53d15c796d190d6a7773a8b74807f81131ffc65f3e8ab", "Current admitted native backend does not provide MSAADepth Resolve Filter; not a hardware inability claim.", false, 0},
    {"supportsGFXIndirectCommandBuffers", "B16@0:8", "0x187aabf08", "dd5f74bbdeeb7b6a6a14fe3d564f58fcc281f2bede928066ed7f23c470c5068c", "Current admitted native backend does not provide GFXIndirect Command Buffers; not a hardware inability claim.", false, 0},
    {"supportsCMPIndirectCommandBuffers", "B16@0:8", "0x187aabfc8", "6472c71cbacd80bb0cda16dab76f2aa92643a7bd2dd5f505ccb7bf3822e2e7a0", "Current admitted native backend does not provide CMPIndirect Command Buffers; not a hardware inability claim.", false, 0},
    {"supportsIndirectStageInRegion", "B16@0:8", "0x187aabed0", "64aa12112f40a87803e0c06d9434ac6bf5f9796fd570acbfe0adcd89141e035c", "Current admitted native backend does not provide Indirect Stage In Region; not a hardware inability claim.", false, 0},
    {"supportsIndirectTextures", "B16@0:8", "0x187aabec0", "0a2043d40e5478e5c7df245c733369d1fb2e993d992faaa8eea62bd233084210", "Current admitted native backend does not provide Indirect Textures; not a hardware inability claim.", false, 0},
    {"supportsNorm16BCubicFiltering", "B16@0:8", "0x187aabc08", "66c5cb41740d1b6bcd3213ec1b2054e46f3bd8e421b5ac2aefeb292cb55fabc3", "Current admitted native backend does not provide Norm16 BCubic Filtering; not a hardware inability claim.", false, 0},
    {"supportsTextureOutOfBoundsReads", "B16@0:8", "0x187aabcb8", "361c7c052b551afd2512868aeadaed00ec5d17ff94de3d7360aa4b25ecea2002", "Current admitted native backend does not provide Texture Out Of Bounds Reads; not a hardware inability claim.", false, 0},
    {"supportsTextureSwizzle", "B16@0:8", "0x187aab99c", "bd25d554cc72a637409819ecdb08609da7cd3662a15f99ae1449cb4b66e1aa56", "Current admitted native backend does not provide Texture Swizzle; not a hardware inability claim.", false, 0},
    {"supportsAlphaYUVFormats", "B16@0:8", "0x187aac010", "255698da0e28d7c7d1f17f9e720531b06ab0dd244f5529a7165d5a89f425b375", "Current admitted native backend does not provide Alpha YUVFormats; not a hardware inability claim.", false, 0},
    {"supportsMemoryOrderAtomics", "B16@0:8", "0x187ac77b0", "4d01f00e9c08d77776cf4946bcec4c1f773732c2ab415f3e4a26254b9cfec4e7", "Current admitted native backend does not provide Memory Order Atomics; not a hardware inability claim.", false, 0},
    {"supportsQuadGroup", "B16@0:8", "0x187aabdf0", "5ce427f7e381b7f307d31790abba18f84cb982248c547231b8720ae0f7f45989", "Current admitted native backend does not provide Quad Group; not a hardware inability claim.", false, 0},
    {"supportsRenderTextureWrites", "B16@0:8", "0x187aabd40", "65478934f177a57681dc00fab9286a408f70294347d315f49ebbcf32051628a1", "Current admitted native backend does not provide Render Texture Writes; not a hardware inability claim.", false, 0},
    {"supportsImageBlocks", "B16@0:8", "0x187aabee0", "19ec5403ed57aaa0391162386fdd5ea98004d43d82ad458a01c6828a06dba10c", "Current admitted native backend does not provide Image Blocks; not a hardware inability claim.", false, 0},
    {"supportsTileShaders", "B16@0:8", "0x187aabc58", "3638eb0980486560ca661221fc21b005ce68ec8c376fe4123697b5527e3c51ae", "Current admitted native backend does not provide Tile Shaders; not a hardware inability claim.", false, 0},
    {"supportsImageBlockSampleCoverageControl", "B16@0:8", "0x187aabee8", "8611e5c52991e1b342b7cdb5c5f90872c85c93454f9f493ca44c2c779ed31e29", "Current admitted native backend does not provide Image Block Sample Coverage Control; not a hardware inability claim.", false, 0},
    {"supportsNativeHardwareFP16", "B16@0:8", "0x187aabc70", "167eebf7c9432d15feac92774c6eb05c34244a02483189aafe897cf6e0a79499", "Current admitted native backend does not provide Native Hardware FP16; not a hardware inability claim.", false, 0},
    {"supportsPostDepthCoverage", "B16@0:8", "0x187aabe20", "f912d7e270182e9275f2d5829e0f7fad9bbea51bc97b6c6833ba496130232f1c", "Current admitted native backend does not provide Post Depth Coverage; not a hardware inability claim.", false, 0},
    {"supportsMipLevelsSmallerThanBlockSize", "B16@0:8", "0x187aabe68", "d0c3b427425d6233e9534d0450ad54cba0c52e8e592219d44a9674e04537e323", "Current admitted native backend does not provide Mip Levels Smaller Than Block Size; not a hardware inability claim.", false, 0},
    {"supportsNonUniformThreadgroupSize", "B16@0:8", "0x187aabbb0", "b1eedbf62d878f635765baba53128a6194081f4d9dc468709e5fca6cdf30d047", "Current admitted native backend does not provide Non Uniform Threadgroup Size; not a hardware inability claim.", false, 0},
    {"supportsReadWriteTextureArguments", "B16@0:8", "0x187aabd68", "6e4db05a0e1035c21f9382d0f4ff52aaf89bae3ed208d5cb6f864172a1b13870", "Current admitted native backend does not provide Read Write Texture Arguments; not a hardware inability claim.", false, 0},
    {"supportsReadWriteTextureCubeArguments", "B16@0:8", "0x187aabba0", "9bf44d9ac346d3e4ca99601386f8733a4fccc88b3d7326007103e37c33aa3804", "Current admitted native backend does not provide Read Write Texture Cube Arguments; not a hardware inability claim.", false, 0},
    {"supportsTextureCubeArray", "B16@0:8", "0x187aabb90", "e94a6057ce53462242eb53656e3c33260a921a3cf06882d062de9e83887e77c8", "Current admitted native backend does not provide Texture Cube Array; not a hardware inability claim.", false, 0},
    {"supportsQuadShufflesAndBroadcast", "B16@0:8", "0x187aabc00", "26e4e240036ddee55bde2b4fff5052a1e3c3c4b3da4ba694d59aafbf766bd384", "Current admitted native backend does not provide Quad Shuffles And Broadcast; not a hardware inability claim.", false, 0},
    {"supportsConcurrentComputeDispatch", "B16@0:8", "0x187aabfa0", "fd155a9118a81d0adab6ce56b4af1d47eee2d5b9a108cc262f997781ae1d6088", "Current admitted native backend does not provide Concurrent Compute Dispatch; not a hardware inability claim.", false, 0},
    {"supportsRenderPassWithoutRenderTarget", "B16@0:8", "0x187abc150", "8a1530f47dd9f1bbc1ec399525c693627572d99c1933f276647e4b5c45df9f3c", "Current admitted native backend does not provide Render Pass Without Render Target; not a hardware inability claim.", false, 0},
    {"supportsRasterOrderGroups", "B16@0:8", "0x187aabc68", "ca0ef7b654593a843f9b17be41eb054b649fd29b04fc505fd30cc3af3313436b", "Current admitted native backend does not provide Raster Order Groups; not a hardware inability claim.", false, 0},
    {"supportsRasterOrderGroupsColorAttachment", "B16@0:8", "0x187aabdd0", "c4a96dfa970febb0bdf8ae476165e094e2f18b6247b780a001d3363d3e896b38", "Current admitted native backend does not provide Raster Order Groups Color Attachment; not a hardware inability claim.", false, 0},
    {"supportsDynamicAttributeStride", "B16@0:8", "0x187aabf80", "0393daefec9e1f1b41e5a19ddd499955df6492f9c95cf5cdb91cc86a8015cdf9", "Current admitted native backend does not provide Dynamic Attribute Stride; not a hardware inability claim.", false, 0},
    {"supportsLinearTexture2DArray", "B16@0:8", "0x187aabe90", "669b5e0013260a150ecbdd2acb0ed5a8271c9c290202680a425048c77efd9a38", "Current admitted native backend does not provide Linear Texture2 DArray; not a hardware inability claim.", false, 0},
    {"supportsNonSquareTileShaders", "B16@0:8", "0x187aabe58", "9bbca45170af2db6b5a45e04bb632962cf986965957a39502d64d41b7927afd9", "Current admitted native backend does not provide Non Square Tile Shaders; not a hardware inability claim.", false, 0},
    {"supportsSeparateVisibilityAndShadingRate", "B16@0:8", "0x187abc158", "0a0d31043e2354d967d039e718f1a3c9776b5b639122f257c1341863ccd2bba3", "Current admitted native backend does not provide Separate Visibility And Shading Rate; not a hardware inability claim.", false, 0},
    {"supports2DLinearTexArraySPI", "B16@0:8", "0x187aac040", "25770a546d2b4c67a49c40c98d50fd8fadb79627429ce1441ee4d09f3761d0af", "Current admitted native backend does not provide 2 DLinear Tex Array SPI; not a hardware inability claim.", false, 0},
    {"supportsLayeredRendering", "B16@0:8", "0x187aabbc8", "d4ec87e3ca2daab0cb83502446e24f138185e06ba1052e25af87fd5e445953dc", "Current admitted native backend does not provide Layered Rendering; not a hardware inability claim.", false, 0},
    {"supportsViewportAndScissorArray", "B16@0:8", "0x187aabca8", "c8434e1ce1c8182bd547725a3aa47273b1e97332e5d745100639962d64f9fd2b", "Current admitted native backend does not provide Viewport And Scissor Array; not a hardware inability claim.", false, 0},
    {"supportsIndirectTessellation", "B16@0:8", "0x187aabec8", "8902dd6b8ab7a9d0e2e586f74fb1880a306f96d86d28941dfb38cf40d4a91b6e", "Current admitted native backend does not provide Indirect Tessellation; not a hardware inability claim.", false, 0},
    {"supportsMSAAStencilResolveFilter", "B16@0:8", "0x187aabe78", "b8fba0ec25da1078dcf3975d0a2e5d01efa6322808d1149281a78100aa4bc353", "Current admitted native backend does not provide MSAAStencil Resolve Filter; not a hardware inability claim.", false, 0},
    {"supportsStencilFeedback", "B16@0:8", "0x187aabcc0", "307dac0eb56b1e740dfd930f22d76bb8d8979635ebade7dbae26f37c647495ca", "Current admitted native backend does not provide Stencil Feedback; not a hardware inability claim.", false, 0},
    {"supportsFP32TessFactors", "B16@0:8", "0x187aabf60", "1ac1570320f0e928180947c3078659f57e8d41d7fb411467a25338db86af42d4", "Current admitted native backend does not provide FP32 Tess Factors; not a hardware inability claim.", false, 0},
    {"supportsUnalignedVertexFetch", "B16@0:8", "0x187a9af48", "5c4ed90f6c55d5bca9cecbeeb8c900970501c0091cffb8b7615ebfa1ff327cc3", "Current admitted native backend does not provide Unaligned Vertex Fetch; not a hardware inability claim.", false, 0},
    {"supportsExtendedVertexFormats", "B16@0:8", "0x187aabf70", "2dcb2838c2488c11ca812db7193eec2c5178cc14219dbbe93f07682f53b22473", "Current admitted native backend does not provide Extended Vertex Formats; not a hardware inability claim.", false, 0},
    {"supportsSIMDGroup", "B16@0:8", "0x187aabc60", "c992fd79b434cef8bbf5ced72ce0c70a646dcf0eb3a7cf9ac17080286fe163ea", "Current admitted native backend does not provide SIMDGroup; not a hardware inability claim.", false, 0},
    {"supportsShaderMinLODClamp", "B16@0:8", "0x187b53660", "5e64bb3630a9eae3d58c814a0e2f9d719eba411c39b344a2d4ec3786dbacca21", "Current admitted native backend does not provide Shader Min LODClamp; not a hardware inability claim.", false, 0},
    {"supportsSIMDShufflesAndBroadcast", "B16@0:8", "0x187aabbe8", "fe0741c515a3a4f82a894fb76eac7dfe7999f9fc3da83bea90adcb1aec561c6d", "Current admitted native backend does not provide SIMDShuffles And Broadcast; not a hardware inability claim.", false, 0},
    {"supportsWritableArrayOfTextures", "B16@0:8", "0x187aabca0", "af2f04cecf0bcfaf627571d1a82e46eb450895689d854d5a778a649d52e9a01f", "Current admitted native backend does not provide Writable Array Of Textures; not a hardware inability claim.", false, 0},
    {"supportsVariableRateRasterization", "B16@0:8", "0x187aabcb0", "97d8c2e4b22dfea8ada39f066915a57484198df5cbbc3515b74bed5d9cbcf073", "Current admitted native backend does not provide Variable Rate Rasterization; not a hardware inability claim.", false, 0},
    {"supportsYCBCRFormats", "B16@0:8", "0x187aabc88", "e431dfda497cb080b24c40a1c4a0e8133e6e44632de1ce68f3350c6482a7f966", "Current admitted native backend does not provide YCBCRFormats; not a hardware inability claim.", false, 0},
    {"supportsYCBCRFormatsPQ", "B16@0:8", "0x187aabc98", "2daf81d8608c53a0559cb28d2f286214b563c8ecfa20a74c687d518e4d6ccabd", "Current admitted native backend does not provide YCBCRFormats PQ; not a hardware inability claim.", false, 0},
    {"supportsYCBCRFormats12", "B16@0:8", "0x187aabc50", "8a4c494def96f5ea3207dcde1ac4615060ee569920b80e7b9b42ce07a4fb20f1", "Current admitted native backend does not provide YCBCRFormats12; not a hardware inability claim.", false, 0},
    {"supportsYCBCRFormatsXR", "B16@0:8", "0x187aabc90", "8a4c494def96f5ea3207dcde1ac4615060ee569920b80e7b9b42ce07a4fb20f1", "Current admitted native backend does not provide YCBCRFormats XR; not a hardware inability claim.", false, 0},
    {"supportsASTCHDRTextureCompression", "B16@0:8", "0x187aac020", "51a0b83bf2bde93892e09418213e28a2e2decbba6935ee7841eb0b8b55fcc59d", "Current admitted native backend does not provide ASTCHDRTexture Compression; not a hardware inability claim.", false, 0},
    {"supportsSparseTextures", "B16@0:8", "0x187aabcd8", "ff1f8d666f190965d3e3ed7834dedf49cf77195a471fb2ca45f984d0a4ab530f", "Current admitted native backend does not provide Sparse Textures; not a hardware inability claim.", false, 0},
    {"supportsSparseHeaps", "B16@0:8", "0x187aabce0", "b1ef8cb42510f2d0a884d9a845de952072175888e192f93e7eb9e047352a683f", "Current admitted native backend does not provide Sparse Heaps; not a hardware inability claim.", false, 0},
    {"supportsIndirectWritableTextures", "B16@0:8", "0x187aabeb8", "cd3939621a9f0d132e36e70d5e7adbde0fe51653a2719675e31b26db61d4d526", "Current admitted native backend does not provide Indirect Writable Textures; not a hardware inability claim.", false, 0},
    {"supportsFunctionPointers", "B16@0:8", "0x187aabf10", "46fb12cd01ac141050b14e5fbf66cbe7907e43f32baac7ce6a2bb27c63f2954a", "Current admitted native backend does not provide Function Pointers; not a hardware inability claim.", false, 0},
    {"supportsDynamicLibraries", "B16@0:8", "0x187b53668", "05a1992953da091063d2ffb9aeb6b08925b427e98d11e630b2982663389a64a9", "Current admitted native backend does not provide Dynamic Libraries; not a hardware inability claim.", false, 0},
    {"supportsStatefulDynamicLibraries", "B16@0:8", "0x187aabcc8", "e29fed8741e5d0e80e28c67e769a524433754ca870c315d1f26cc0a5b1f2e423", "Current admitted native backend does not provide Stateful Dynamic Libraries; not a hardware inability claim.", false, 0},
    {"supportsRenderDynamicLibraries", "B16@0:8", "0x187aabd58", "fc86ec358fc37d7eb61266e8a04360eae1db67e741d9721ea146a326fff31102", "Current admitted native backend does not provide Render Dynamic Libraries; not a hardware inability claim.", false, 0},
    {"supportsFunctionPointersFromRender", "B16@0:8", "0x187aabf18", "ed35673895840ba75e058c24537854c5acc84e4a615586678106cb4cd8715d4d", "Current admitted native backend does not provide Function Pointers From Render; not a hardware inability claim.", false, 0},
    {"supportsSharedFunctionTables", "B16@0:8", "0x187aabd00", "97543652f5fb8d9f7d5a2111f5e5b832e820e80115ca498ab3ce419f4c8f3dd9", "Current admitted native backend does not provide Shared Function Tables; not a hardware inability claim.", false, 0},
    {"supportsRaytracingFromRender", "B16@0:8", "0x187aabd78", "c5b0e82fa4c4b97f2e9825efb4ff1c503b305b01248c4427b5b13bbc10387e7c", "Current admitted native backend does not provide Raytracing From Render; not a hardware inability claim.", false, 0},
    {"supportsPrimitiveMotionBlur", "B16@0:8", "0x187aabe18", "c64a6cbb5f79d845b4deba5ee1740178d061696b59dc954d2618b75ad7db4630", "Current admitted native backend does not provide Primitive Motion Blur; not a hardware inability claim.", false, 0},
    {"supportsRayTracingExtendedVertexFormats", "B16@0:8", "0x187aabdb0", "25931e1ac9393a45b851ca5183ce3178f0a5b6bd12db5687c88a271a730f12a4", "Current admitted native backend does not provide Ray Tracing Extended Vertex Formats; not a hardware inability claim.", false, 0},
    {"supportsHeapAccelerationStructureAllocation", "B16@0:8", "0x187aabef8", "94fd501fc46026779c431473ab753364d072702c07ddcf0a85dd46040ac09a5f", "Current admitted native backend does not provide Heap Acceleration Structure Allocation; not a hardware inability claim.", false, 0},
    {"supportsRayTracingPerPrimitiveData", "B16@0:8", "0x187aabd88", "bcbc42def8513eb4282a641b3d75bb98ff67d48af8e91573751836a26b02c1e6", "Current admitted native backend does not provide Ray Tracing Per Primitive Data; not a hardware inability claim.", false, 0},
    {"supportsRayTracingTraversalMetrics", "B16@0:8", "0x187aabd80", "dc09da7e8e1b50219704ef09d14437e3102b0235094214f49429700821febd8d", "Current admitted native backend does not provide Ray Tracing Traversal Metrics; not a hardware inability claim.", false, 0},
    {"supportsRayTracingBuffersFromTables", "B16@0:8", "0x187aabdc0", "f3c2d1a0f9bfbc78665123d31068b00358d863df75bbaf110676eb9da5fc6aa9", "Current admitted native backend does not provide Ray Tracing Buffers From Tables; not a hardware inability claim.", false, 0},
    {"supportsRayTracingAccelerationStructureCPUDeserialization", "B16@0:8", "0x187aabdc8", "bef8ff6d8b9142717213a223d87649be051f471dfda77da914bf1b41825a33fb", "Current admitted native backend does not provide Ray Tracing Acceleration Structure CPUDeserialization; not a hardware inability claim.", false, 0},
    {"supportsRayTracingMultiLevelInstancing", "B16@0:8", "0x187aabd90", "f6f36306825b7e0fcd22af3c4e6f58805ca7f3f2e3acc5ab1093933dace8b4fd", "Current admitted native backend does not provide Ray Tracing Multi Level Instancing; not a hardware inability claim.", false, 0},
    {"supportsRayTracingIndirectInstanceAccelerationStructureBuild", "B16@0:8", "0x187aabd98", "99a77d62fc56c35bf66bd1ce552e4f3fb806c67aeb72b002ee8d3ad6235d1b71", "Current admitted native backend does not provide Ray Tracing Indirect Instance Acceleration Structure Build; not a hardware inability claim.", false, 0},
    {"supportsRayTracingGPUTableUpdateBuffers", "B16@0:8", "0x187aabda8", "de5c46b6819a098f00b5a63b47852a76d1f1ca42c1e7d41ea51e971672d3acd7", "Current admitted native backend does not provide Ray Tracing GPUTable Update Buffers; not a hardware inability claim.", false, 0},
    {"supportsRayTracingCurves", "B16@0:8", "0x187aabdb8", "cbf186ac84b65cc55908d6b671539d70bde0609fbb39b92b2e9420588ca7a5d1", "Current admitted native backend does not provide Ray Tracing Curves; not a hardware inability claim.", false, 0},
    {"supportsShaderBarycentricCoordinates", "B16@0:8", "0x187b53670", "7c31a34fa99478cff5e082ef9e1870afcb589c1d841df82b1e42050b4caf7ff0", "Current admitted native backend does not provide Shader Barycentric Coordinates; not a hardware inability claim.", false, 0},
    {"supportsBlackOrWhiteSamplerBorderColors", "B16@0:8", "0x187b53678", "28065572e7a7ca2831c7442133bea9b6c00e686c6c657860f6cfaea39ce69255", "Current admitted native backend does not provide Black Or White Sampler Border Colors; not a hardware inability claim.", false, 0},
    {"supportsMirrorClampToEdgeSamplerMode", "B16@0:8", "0x187b53680", "578531b1b6011743bd2f922974b02338a062dbe931a5cc294a220eaff496149a", "Current admitted native backend does not provide Mirror Clamp To Edge Sampler Mode; not a hardware inability claim.", false, 0},
    {"supportsSIMDReduction", "B16@0:8", "0x187aabbf0", "3a0707ee4a9ddeea398c03e17d5fdb56a33f518cf385bdaa193d72ff1e835ea3", "Current admitted native backend does not provide SIMDReduction; not a hardware inability claim.", false, 0},
    {"supportsDepthClipModeClampExtended", "B16@0:8", "0x187aabf90", "3b4513483ffe8b93b611f4818255c7a6f40a8953ab13b69d5a2e2f49cfd3cf4e", "Current admitted native backend does not provide Depth Clip Mode Clamp Extended; not a hardware inability claim.", false, 0},
    {"supportsTexture2DMultisampleArray", "B16@0:8", "0x187b53688", "9d3ee90d92078508af2952ff5526ac7b21b55fb1fbd46dcd1998840d1657193e", "Current admitted native backend does not provide Texture2 DMultisample Array; not a hardware inability claim.", false, 0},
    {"supportsForceSeamsOnCubemaps", "B16@0:8", "0x187aabf38", "4728ec113e448cf6ddba57d704d4bf14661b300574119d56f82dfdb81694f7c2", "Current admitted native backend does not provide Force Seams On Cubemaps; not a hardware inability claim.", false, 0},
    {"supportsFloat16BCubicFiltering", "B16@0:8", "0x187aabc10", "4431a63f21501d241801e723baab46a4c1fcd30cc2a21616eb45094312d0a24f", "Current admitted native backend does not provide Float16 BCubic Filtering; not a hardware inability claim.", false, 0},
    {"supportsFloat16InfNanFiltering", "B16@0:8", "0x187aabf40", "37d1350cb5c513e84eee82ee26376a58d674ad5ed681ce7a788f08a00056d1bd", "Current admitted native backend does not provide Float16 Inf Nan Filtering; not a hardware inability claim.", false, 0},
    {"supportsRTZRounding", "B16@0:8", "0x187aabdd8", "35bb9d572d13f3b1aff579be5a10b6861a3b1e7650ea0ac20e71b0addc883c6f", "Current admitted native backend does not provide RTZRounding; not a hardware inability claim.", false, 0},
    {"supportsAnisoSampleFix", "B16@0:8", "0x187aac008", "362437dcc93981b651d05ce2ffbdf49d05c6b52a7ec0d3d070e444725d2e81c2", "Current admitted native backend does not provide Aniso Sample Fix; not a hardware inability claim.", false, 0},
    {"supportsYCBCRPackedFormatsPQ", "B16@0:8", "0x187aabc80", "f087f8c6a1d68aea02d1e9838f84ddec3636422472a788780eed7dfb1f3df7b1", "Current admitted native backend does not provide YCBCRPacked Formats PQ; not a hardware inability claim.", false, 0},
    {"supportsYCBCRPackedFormats12", "B16@0:8", "0x187aabc48", "b08992659b9de4279f67651a41dbf13f27ccf9c12810b77e8819cc00b488fea7", "Current admitted native backend does not provide YCBCRPacked Formats12; not a hardware inability claim.", false, 0},
    {"supportsYCBCRPackedFormatsXR", "B16@0:8", "0x187aabc78", "20489835cf4084aa2d7b28191114e26376f9dd5704d7f62f57594404e4b7c672", "Current admitted native backend does not provide YCBCRPacked Formats XR; not a hardware inability claim.", false, 0},
    {"supportsBufferBoundsChecking", "B16@0:8", "0x187b53690", "ba4acdfb39b889e8fea7af68d68439fdfd58da6d1ce32c779de1d93e0e29a39f", "Current general shader buffer-bounds checking contract is not provided: Compute constants[96+i] and exact logical-byte metadata plus fixed AIR guard probes prove only admitted shaders, not arbitrary OOB behavior.", false, 0},
    {"supportsForkJoin", "B16@0:8", "0x187aabf30", "39c1253c967c6d71b676de125b0b3208fdd2321ccc277bdaf10247b2272ac39c", "Current admitted native backend does not provide Fork Join; not a hardware inability claim.", false, 0},
    {"supportsDevicePartitioning", "B16@0:8", "0x187aabf88", "2f2c7547ee1553b2f524749998b534e98a3b979612d7c4c25f8ffe19b14c2401", "Current admitted native backend does not provide Device Partitioning; not a hardware inability claim.", false, 0},
    {"supportsComputeCompressedTextureWrite", "B16@0:8", "0x187b53698", "eb041c10c45dd2485ac334f7ca5e30d3f01f0a23a0e4a5100d0aa7b9e54384b7", "Current admitted native backend does not provide Compute Compressed Texture Write; not a hardware inability claim.", false, 0},
    {"supportsSIMDGroupMatrix", "B16@0:8", "0x187aabd28", "cb55dab769e0572c193b1c167de31b9e840bc800debb01cceabf3fde4691ef56", "Current admitted native backend does not provide SIMDGroup Matrix; not a hardware inability claim.", false, 0},
    {"supportsInterchangeTiled", "B16@0:8", "0x187aabea8", "3fd27fdde41144462cddf08bee612227f47e0ed798dd248302c504299dde6810", "Current admitted native backend does not provide Interchange Tiled; not a hardware inability claim.", false, 0},
    {"supportsQuadReduction", "B16@0:8", "0x187aabde8", "2a624e793614e81fa715f4459b4b898d5f5ec97d3b97ca8863f02138a9cc4a87", "Current admitted native backend does not provide Quad Reduction; not a hardware inability claim.", false, 0},
    {"supportsVirtualSubstreams", "B16@0:8", "0x187b536a0", "1a39c12ef28b079bad906bd08246a1ddf29509137d2282317d98def93e86aae3", "Current admitted native backend does not provide Virtual Substreams; not a hardware inability claim.", false, 0},
    {"supportsSIMDShuffleAndFill", "B16@0:8", "0x187aabbe0", "872411cd9f0638fe5e2fc75e27d4ce3cf585c8a316459e63d151a5acd92ed195", "Current admitted native backend does not provide SIMDShuffle And Fill; not a hardware inability claim.", false, 0},
    {"supportsBfloat16Format", "B16@0:8", "0x187aabfe8", "9f81d6649d86659671aa38eafd0c03fd198e576299580c95d0589c6c303be767", "Current admitted native backend does not provide Bfloat16 Format; not a hardware inability claim.", false, 0},
    {"supportsSparseDepthAttachments", "B16@0:8", "0x187aabce8", "b40a7c08388f8bd95775bd0b778a37e1485b4ff1d81f23435e33420c5746fafc", "Current admitted native backend does not provide Sparse Depth Attachments; not a hardware inability claim.", false, 0},
    {"supportsAtomicUlongVoidMinMax", "B16@0:8", "0x187b536a8", "2cb5c7c7b09cbcce67faea7e9aaddacb712346c02dff28e455f8a3725e0bcece", "Current admitted native backend does not provide Atomic Ulong Void Min Max; not a hardware inability claim.", false, 0},
    {"supportsLossyCompression", "B16@0:8", "0x187a827f4", "e87aefc170ea8a9a7afacaf946159513f343c12d5343f379a3938cdeffa730a1", "Current admitted native backend does not provide Lossy Compression; not a hardware inability claim.", false, 0},
    {"supportsMeshShaders", "B16@0:8", "0x187b536b0", "13424f1d96866da85a85c1434921bb2a2158569bb7130df3c7b787f744c8aa49", "Current admitted native backend does not provide Mesh Shaders; not a hardware inability claim.", false, 0},
    {"supportsFunctionPointersFromMesh", "B16@0:8", "0x187aabf20", "ec03fb0fd4225e2c0eabbaad956cc5fd59887f402d8196a5f7c454baca406764", "Current admitted native backend does not provide Function Pointers From Mesh; not a hardware inability claim.", false, 0},
    {"supportsMeshShadersInICB", "B16@0:8", "0x187aabe70", "af3079c4a71b914447d2348c24af600e294c34e79fbe95c23538cb40927990d4", "Current admitted native backend does not provide Mesh Shaders In ICB; not a hardware inability claim.", false, 0},
    {"supportsBfloat16Buffers", "B16@0:8", "0x187a9af50", "4bc20883fadd8a4ee46f9479c0cffdf13c9d9f94c94a5cc159f341c11ed935e1", "Current admitted native backend does not provide Bfloat16 Buffers; not a hardware inability claim.", false, 0},
    {"supportsCommandBufferJump", "B16@0:8", "0x187aabc20", "c437813ffe1f5be7a8ed6020845a71e06dea0b59cd3efcd80da01b5324b60a17", "Current admitted native backend does not provide Command Buffer Jump; not a hardware inability claim.", false, 0},
    {"supportsColorSpaceConversionMatrixSelection", "B16@0:8", "0x187aabfc0", "0e9d1c976bf2a543770f960529f60c08aecd8b15f9a7486673a3d52540133197", "Current admitted native backend does not provide Color Space Conversion Matrix Selection; not a hardware inability claim.", false, 0},
    {"supportsPerPlaneCompression", "B16@0:8", "0x187b536b8", "1c2968d84c17f9c49a95d28474451376df87418324850a618c7a6de2a31cc9f0", "Current admitted native backend does not provide Per Plane Compression; not a hardware inability claim.", false, 0},
    {"supportsAtomicWaitNotify", "B16@0:8", "0x187aabc28", "2f2c7547ee1553b2f524749998b534e98a3b979612d7c4c25f8ffe19b14c2401", "Current admitted native backend does not provide Atomic Wait Notify; not a hardware inability claim.", false, 0},
    {"supportsConditionalLoadStore", "B16@0:8", "0x187aabc18", "6a19ca030e57e9ecbc1296535c6aa197389d7e59a59f449d17b303737333f0a1", "Current admitted native backend does not provide Conditional Load Store; not a hardware inability claim.", false, 0},
    {"supportsStackOverflowErrorCode", "B16@0:8", "0x187aabcd0", "bdcd1c0a43382e76b7ae8ca00474ab6685047f06b40b2823a741a26c8f38331a", "Current admitted native backend does not provide Stack Overflow Error Code; not a hardware inability claim.", false, 0},
    {"supportsRayTracingICBs", "B16@0:8", "0x187aabda0", "ea70ddf37cb18ca0c8d70179c04d2ce7efbf9fe10cbe725c44a9d30099b6566a", "Current admitted native backend does not provide Ray Tracing ICBs; not a hardware inability claim.", false, 0},
    {"supportsPerformanceStateAssertion", "B16@0:8", "0x187aabe38", "bd14b17e4fabfe262e6d3e5d2cc1f759be863e695c8a0b0cc79dbb1481340fd0", "Current admitted native backend does not provide Performance State Assertion; not a hardware inability claim.", false, 0},
    {"supportsExplicitVisibilityGroups", "B16@0:8", "0x187b536c0", "53ef96cee24c68b151f9286c0a4bbac8a4a92e948ab1f0deccf1e73d43a47de9", "Current admitted native backend does not provide Explicit Visibility Groups; not a hardware inability claim.", false, 0},
};
static_assert(sizeof(Policy)/sizeof(Policy[0]) == QueryCount, "target coverage");
const Entry *find(const char *name) {
    if (!name) return nullptr;
    for (const auto &entry : Policy)
        if (!std::strcmp(name, entry.selector)) return &entry;
    return nullptr;
}
// Actual Objective-C BOOL callback; no self ivar/context/feature object access.
// An unknown _cmd is an invariant error, not NO/YES: abort instead of classify.
BOOL query(id self, SEL command) {
    (void)self;
    const Entry *entry = command ? find(sel_getName(command)) : nullptr;
    if (!entry) std::abort();
    return entry->supported ? YES : NO;
}
bool validatePolicy() {
    size_t yes = 0;
    for (size_t i=0; i<QueryCount; ++i) {
        const Entry &e=Policy[i];
        if (!e.selector || !e.selector[0] || std::strchr(e.selector, ':') ||
            !e.type || std::strcmp(e.type,"B16@0:8") || !e.reason || !e.reason[0] ||
            !e.targetIMP || !e.targetIMPSHA256 || std::strlen(e.targetIMPSHA256)!=64 ||
            e.requiredEvidenceFlags != (e.supported ? Required : 0)) return false;
        yes += e.supported;
        if (e.supported && std::strcmp(e.selector,"supportsRenderToLinearTextures")) return false;
        for (size_t j=0; j<i; ++j)
            if (!std::strcmp(e.selector, Policy[j].selector)) return false;
    }
    return yes==1;
}
}
const Entry *entries() { return Policy; }
size_t count() { return QueryCount; }
uint32_t minimumEvidenceFlags() { return Required; }
Status answer(const char *selector, BOOL &value, const char *&reason) {
    const Entry *entry=find(selector);
    if (!entry) return Status::UnknownSelector;
    value=entry->supported ? YES : NO; reason=entry->reason;
    return Status::Ok;
}
}
bool NativeMetal540InstallQueries(Class subclass, Class base,
    const NativeMetal540Capabilities::Evidence &evidence,
    NativeMetal540Capabilities::Report &report) {
    using namespace NativeMetal540Capabilities;
    report=Report{};
    auto fail=[&](Status status,const char *selector=nullptr) {
        report.status=status; report.selector=selector; return false;
    };
    if (!validatePolicy()) return fail(Status::InvalidPolicy);
    report.missingEvidenceFlags=Required & ~evidence.flags;
    if (report.missingEvidenceFlags) return fail(Status::MissingEvidence);
    if (evidence.flags & ~Required) return fail(Status::InvalidEvidence);
#if defined(NATIVE540_CAPABILITIES_HOST_MODEL)
    // Host tests NEVER use macOS's real _MTLDevice; production cannot opt via data.
    const char *baseName="Native540CapabilitiesMockBase";
#else
    const char *baseName="_MTLDevice";
#endif
    if (!base || !subclass || subclass==base || class_isMetaClass(base) ||
        class_isMetaClass(subclass) || objc_lookUpClass(baseName)!=base ||
        class_getSuperclass(base)!=[NSObject class] ||
        class_getSuperclass(subclass)!=base ||
        std::strncmp(class_getName(subclass),"NativeMetal540",14) ||
        objc_lookUpClass(class_getName(subclass))) return fail(Status::InvalidClass);
    if (class_getInstanceSize(base)!=TargetBaseInstanceSize ||
        class_getInstanceSize(subclass)!=TargetBaseInstanceSize)
        return fail(Status::BaseSizeMismatch);
    unsigned int baseCount=0,subCount=0;
    Method *baseMethods=class_copyMethodList(base,&baseCount);
    Method *subMethods=class_copyMethodList(subclass,&subCount);
    Status error=Status::Ok; const char *bad=nullptr;
    // Direct/own methods only, NOT class_getInstanceMethod's inherited fallback.
    for (size_t i=0; i<QueryCount; ++i) {
        const Entry &entry=Policy[i]; SEL selector=sel_registerName(entry.selector);
        Method method=nullptr;
        for (unsigned int j=0; j<baseCount; ++j)
            if (sel_isEqual(method_getName(baseMethods[j]),selector)) method=baseMethods[j];
        if (!method) { error=Status::BaseMethodMissing; bad=entry.selector; break; }
        const char *type=method_getTypeEncoding(method);
        if (!type || std::strcmp(type,entry.type)) {
            error=Status::TypeMismatch; bad=entry.selector; break;
        }
        for (unsigned int j=0; j<subCount; ++j)
            if (sel_isEqual(method_getName(subMethods[j]),selector)) {
                error=Status::SubclassMethodExists; bad=entry.selector; break;
            }
        if (error!=Status::Ok) break;
        ++report.checked;
    }
    std::free(baseMethods); std::free(subMethods);
    if (error!=Status::Ok) return fail(error,bad);
    report.preflightComplete=true;
    for (const Entry &entry : Policy) {
        if (!class_addMethod(subclass,sel_registerName(entry.selector),
                             reinterpret_cast<IMP>(query),entry.type)) {
            report.discardSubclass=true;
            return fail(Status::AddMethodFailed,entry.selector);
        }
        ++report.installed; report.supported+=entry.supported;
    }
    report.status=Status::Ok; return true;
}
