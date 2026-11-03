; SPDX-License-Identifier: MIT
; Authored numerical test input, not an extracted target component.
source_filename = "air_fma_numerical_test"
target datalayout = "e-p:64:64-i16:16-i64:64-f32:32-n8:16:32"
target triple = "air64-apple-ios17.7.0"
define <4 x float> @fma_numerical_test(ptr addrspace(2) readonly align 4 dereferenceable(24) %buffer, i16 %vertex) {
  %a0 = load float, ptr addrspace(2) %buffer, align 4
  %pa1 = getelementptr float, ptr addrspace(2) %buffer, i64 1
  %a1 = load float, ptr addrspace(2) %pa1, align 4
  %pb0 = getelementptr float, ptr addrspace(2) %buffer, i64 2
  %b0 = load float, ptr addrspace(2) %pb0, align 4
  %pb1 = getelementptr float, ptr addrspace(2) %buffer, i64 3
  %b1 = load float, ptr addrspace(2) %pb1, align 4
  %pc0 = getelementptr float, ptr addrspace(2) %buffer, i64 4
  %c0 = load float, ptr addrspace(2) %pc0, align 4
  %pc1 = getelementptr float, ptr addrspace(2) %buffer, i64 5
  %c1 = load float, ptr addrspace(2) %pc1, align 4
  %atmp = insertelement <2 x float> poison, float %a0, i64 0
  %a = insertelement <2 x float> %atmp, float %a1, i64 1
  %btmp = insertelement <2 x float> poison, float %b0, i64 0
  %b = insertelement <2 x float> %btmp, float %b1, i64 1
  %ctmp = insertelement <2 x float> poison, float %c0, i64 0
  %c = insertelement <2 x float> %ctmp, float %c1, i64 1
  %f = call <2 x float> @air.fma.v2f32(<2 x float> %a, <2 x float> %b, <2 x float> %c)
  %result = shufflevector <2 x float> %f, <2 x float> <float 0.0, float 1.0>, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x float> %result
}
declare <2 x float> @air.fma.v2f32(<2 x float>, <2 x float>, <2 x float>)
!air.version = !{!0}
!air.compile_options = !{!1, !2}
!air.vertex = !{!3}
!0 = !{i32 2, i32 6, i32 0}
!1 = !{!"air.compile.fast_math_enable"}
!2 = !{!"air.compile.denorms_disable"}
!3 = !{ptr @fma_numerical_test, !4, !6}
!4 = !{!5}
!5 = !{!"air.position", !"air.arg_type_name", !"float4"}
!6 = !{!7, !8}
!7 = !{i32 0, !"air.buffer", !"air.buffer_size", i32 24, !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2}
!8 = !{i32 1, !"air.vertex_id"}
