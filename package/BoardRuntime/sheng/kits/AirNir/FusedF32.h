/* SPDX-License-Identifier: BSD-3-Clause
 * Derived from Berkeley SoftFloat Release 3e, s_mulAddF32.c,
 * s_roundPackToF32.c, s_normSubnormalF32Sig.c and shift/jam primitives.
 * Copyright 2011-2018 The Regents of the University of California.
 * All rights reserved. Full notice/conditions/disclaimer: licenses/SoftFloat-3e.txt.
 *
 * NIR integer instruction construction, not a host floating-point operation.
 * Round-to-nearest, ties-to-even, canonical quiet NaNs, no exception flags.
 * 64-bit integer intermediates are subsequently lowered to native GPU 32-bit
 * integer operations by NIR. No floating-point multiply or unfused MAD is used.
 */
#ifndef AIR_FUSED_F32_H
#define AIR_FUSED_F32_H
#include "nir/nir_builder.h"

/* Unlike the upstream primitive this helper also defines distance == 0.
 * Saturation prevents modulo-width GPU shifts from losing the sticky bit. */
static nir_def *air_shift_jam(nir_builder *b, nir_def *v, nir_def *distance) {
    unsigned bits = v->bit_size;
    nir_def *small = nir_ult_imm(b, distance, bits);
    nir_def *d = nir_umin(b, distance, nir_imm_int(b, bits - 1));
    nir_def *shifted = nir_ushr(b, v, d);
    nir_def *lost = nir_ine(b, nir_ishl(b, shifted, d), v);
    nir_def *jammed = nir_ior(b, shifted, nir_b2iN(b, lost, bits));
    return nir_bcsel(b, small, jammed, nir_b2iN(b, nir_ine_imm(b, v, 0), bits));
}
struct air_sig_exp {
    nir_def *sig, *exp;
};
static struct air_sig_exp air_normalize_f32(nir_builder *b, nir_def *ui) {
    nir_def *exp = nir_iand_imm(b, nir_ushr_imm(b, ui, 23), 255);
    nir_def *frac = nir_iand_imm(b, ui, 0x7fffff);
    nir_def *shift = nir_isub(b, nir_imm_int(b, 23), nir_ufind_msb(b, frac));
    nir_def *subnormal = nir_ieq_imm(b, exp, 0);
    return (struct air_sig_exp){
        nir_bcsel(b, subnormal, nir_ishl(b, frac, shift), frac),
        nir_bcsel(b, subnormal, nir_isub(b, nir_imm_int(b, 1), shift), exp)};
}
static nir_def *air_round_f32(nir_builder *b, nir_def *sign, nir_def *exp, nir_def *sig) {
    nir_def *negative_exp = nir_ilt_imm(b, exp, 0);
    nir_def *overflow =
        nir_ior(b, nir_ilt(b, nir_imm_int(b, 253), exp),
                nir_iand(b, nir_ieq_imm(b, exp, 253),
                         nir_uge(b, nir_iadd_imm(b, sig, 0x40), nir_imm_int(b, 0x80000000))));
    sig = nir_bcsel(b, negative_exp, air_shift_jam(b, sig, nir_ineg(b, exp)), sig);
    exp = nir_bcsel(b, negative_exp, nir_imm_int(b, 0), exp);
    nir_def *tie = nir_ieq_imm(b, nir_iand_imm(b, sig, 127), 64);
    sig = nir_ushr_imm(b, nir_iadd_imm(b, sig, 64), 7);
    sig = nir_iand(b, sig, nir_inot(b, nir_b2i32(b, tie)));
    exp = nir_bcsel(b, nir_ieq_imm(b, sig, 0), nir_imm_int(b, 0), exp);
    /* Addition is intentional: the explicit hidden bit carries into exponent. */
    nir_def *packed = nir_iadd(b, nir_iadd(b, sign, nir_ishl_imm(b, exp, 23)), sig);
    return nir_bcsel(b, overflow, nir_ior_imm(b, sign, 0x7f800000), packed);
}
static nir_def *air_fused_f32(nir_builder *b, nir_def *a, nir_def *v, nir_def *c) {
    struct air_sig_exp na = air_normalize_f32(b, a), nb = air_normalize_f32(b, v);
    struct air_sig_exp nc = air_normalize_f32(b, c);
    nir_def *sign_p = nir_iand_imm(b, nir_ixor(b, a, v), 0x80000000);
    nir_def *sign_c = nir_iand_imm(b, c, 0x80000000);
    nir_def *exp_p = nir_iadd_imm(b, nir_iadd(b, na.exp, nb.exp), -126);
    nir_def *ma = nir_ishl_imm(b, nir_ior_imm(b, na.sig, 0x800000), 7);
    nir_def *mb = nir_ishl_imm(b, nir_ior_imm(b, nb.sig, 0x800000), 7);
    nir_def *prod = nir_imul(b, nir_u2u64(b, ma), nir_u2u64(b, mb));
    nir_def *short_product = nir_ult(b, prod, nir_imm_int64(b, UINT64_C(0x2000000000000000)));
    prod = nir_bcsel(b, short_product, nir_ishl_imm(b, prod, 1), prod);
    exp_p = nir_isub(b, exp_p, nir_b2i32(b, short_product));
    nir_def *mc = nir_ishl_imm(b, nir_ior_imm(b, nc.sig, 0x800000), 6);
    nir_def *c64 = nir_ishl_imm(b, nir_u2u64(b, mc), 32);
    nir_def *diff = nir_isub(b, exp_p, nc.exp);
    nir_def *negative = nir_ilt_imm(b, diff, 0);
    nir_def *positive = nir_ilt(b, nir_imm_int(b, 0), diff);
    nir_def *max_exp = nir_bcsel(b, negative, nc.exp, exp_p);

    /* Same-sign addition, preserving all product bits until final rounding. */
    nir_def *sum_c = nir_iadd(
        b, mc, nir_u2u32(b, air_shift_jam(b, prod, nir_isub(b, nir_imm_int(b, 32), diff))));
    nir_def *sum_p = nir_u2u32(
        b, air_shift_jam(b, nir_iadd(b, prod, air_shift_jam(b, c64, diff)), nir_imm_int(b, 32)));
    nir_def *sum = nir_bcsel(b, positive, sum_p, sum_c);
    nir_def *small_sum = nir_ult_imm(b, sum, 0x40000000);
    nir_def *sum_exp = nir_isub(b, max_exp, nir_b2i32(b, small_sum));
    sum = nir_bcsel(b, small_sum, nir_ishl_imm(b, sum, 1), sum);

    /* Opposite-sign subtraction and cancellation normalization. */
    nir_def *equal_difference = nir_isub(b, prod, c64);
    nir_def *equal_negative = nir_ilt_imm(b, equal_difference, 0);
    nir_def *equal_magnitude =
        nir_bcsel(b, equal_negative, nir_ineg(b, equal_difference), equal_difference);
    nir_def *magnitude = nir_bcsel(
        b, negative, nir_isub(b, c64, air_shift_jam(b, prod, nir_ineg(b, diff))),
        nir_bcsel(b, positive, nir_isub(b, prod, air_shift_jam(b, c64, diff)), equal_magnitude));
    nir_def *sub_sign = nir_bcsel(
        b, negative, sign_c,
        nir_bcsel(b, positive, sign_p,
                  nir_ixor(b, sign_p, nir_ishl_imm(b, nir_b2i32(b, equal_negative), 31))));
    nir_def *shift = nir_isub(b, nir_imm_int(b, 62), nir_ufind_msb(b, magnitude));
    nir_def *sub_exp = nir_isub(b, max_exp, shift);
    nir_def *remaining = nir_iadd_imm(b, shift, -32);
    nir_def *sub_sig = nir_bcsel(b, nir_ilt_imm(b, remaining, 0),
                                 nir_u2u32(b, air_shift_jam(b, magnitude, nir_ineg(b, remaining))),
                                 nir_ishl(b, nir_u2u32(b, magnitude), remaining));
    nir_def *same_sign = nir_ieq(b, sign_p, sign_c);
    nir_def *result = air_round_f32(b, nir_bcsel(b, same_sign, sign_p, sub_sign),
                                    nir_bcsel(b, same_sign, sum_exp, sub_exp),
                                    nir_bcsel(b, same_sign, sum, sub_sig));
    result = nir_bcsel(b, nir_iand(b, nir_inot(b, same_sign), nir_ieq_imm(b, magnitude, 0)),
                       nir_imm_int(b, 0), result);
    /* A zero addend bypasses the fictitious normalized significand for zero. */
    nir_def *zero_c = nir_ieq_imm(b, nir_iand_imm(b, c, 0x7fffffff), 0);
    nir_def *only_product = air_round_f32(b, sign_p, nir_iadd_imm(b, exp_p, -1),
                                          nir_u2u32(b, air_shift_jam(b, prod, nir_imm_int(b, 31))));
    result = nir_bcsel(b, zero_c, only_product, result);
    nir_def *zero_a = nir_ieq_imm(b, nir_iand_imm(b, a, 0x7fffffff), 0);
    nir_def *zero_b = nir_ieq_imm(b, nir_iand_imm(b, v, 0x7fffffff), 0);
    result = nir_bcsel(b, nir_ior(b, zero_a, zero_b),
                       nir_bcsel(b, zero_c, nir_iand(b, sign_p, sign_c), c), result);

    /* Canonical quiet NaN; exception flags and NaN-payload compatibility
     * are not implemented or claimed by this compiler path. */
    nir_def *ea = nir_iand_imm(b, nir_ushr_imm(b, a, 23), 255);
    nir_def *eb = nir_iand_imm(b, nir_ushr_imm(b, v, 23), 255);
    nir_def *ec = nir_iand_imm(b, nir_ushr_imm(b, c, 23), 255);
    nir_def *special_a = nir_ieq_imm(b, ea, 255), *special_b = nir_ieq_imm(b, eb, 255);
    nir_def *special_c = nir_ieq_imm(b, ec, 255);
    nir_def *nan_a = nir_iand(b, special_a, nir_ine_imm(b, nir_iand_imm(b, a, 0x7fffff), 0));
    nir_def *nan_b = nir_iand(b, special_b, nir_ine_imm(b, nir_iand_imm(b, v, 0x7fffff), 0));
    nir_def *nan_c = nir_iand(b, special_c, nir_ine_imm(b, nir_iand_imm(b, c, 0x7fffff), 0));
    nir_def *inf_p = nir_ior(b, special_a, special_b);
    nir_def *invalid = nir_iand(
        b, inf_p,
        nir_ior(b, nir_ior(b, zero_a, zero_b), nir_iand(b, special_c, nir_ine(b, sign_p, sign_c))));
    result = nir_bcsel(b, special_c, c, result);
    result = nir_bcsel(b, inf_p, nir_ior_imm(b, sign_p, 0x7f800000), result);
    return nir_bcsel(b, nir_ior(b, invalid, nir_ior(b, nan_a, nir_ior(b, nan_b, nan_c))),
                     nir_imm_int(b, 0x7fc00000), result);
}
#endif
