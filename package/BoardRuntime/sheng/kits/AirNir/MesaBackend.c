/* SPDX-License-Identifier: MIT */
#include "AirNir.h"
#include "BackendABI.h"
#include "FusedF32.h"
#include "compiler/glsl_types.h"
#include "freedreno_dev_info.h"
#include "ir3/ir3_compiler.h"
#include "ir3/ir3_shader.h"
#include "nir/nir_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct air_nir {
    struct ir3_compiler *compiler;
    struct fd_dev_id dev_id;
    struct air_constant_read reads[AIR_CONSTANT_MAX_READS];
    unsigned read_count;
    nir_builder b;
    unsigned outputs;
    bool allow_reciprocal;
    unsigned reciprocal_lowerings;
};
struct air_nir *air_nir_create_stage(uint64_t chip, const char *name, enum air_stage stage) {
    if (stage != AIR_VERTEX && stage != AIR_FRAGMENT)
        return NULL;
    const struct fd_dev_id id = {.chip_id = chip};
    const struct fd_dev_info *info = fd_dev_info_raw(&id);
    if (!info || fd_dev_gen(&id) != 7)
        return NULL;
    struct air_nir *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->dev_id = id;
    const struct ir3_compiler_options options = {
        .disable_cache = true, .bindless_fb_read_descriptor = -1, .bindless_fb_read_slot = -1};
    s->compiler = ir3_compiler_create(NULL, &s->dev_id, info, &options);
    if (!s->compiler || !(ir3_shader_debug & IR3_DBG_ASM_ROUNDTRIP)) {
        if (s->compiler)
            ir3_compiler_destroy(s->compiler);
        free(s);
        return NULL;
    }
    glsl_type_singleton_init_or_ref();
    s->b = nir_builder_init_simple_shader(stage == AIR_VERTEX ? MESA_SHADER_VERTEX : MESA_SHADER_FRAGMENT, ir3_get_compiler_options(s->compiler),
                                          "%s", name);
    s->b.shader->info.internal = false;
    return s;
}
struct air_nir *air_nir_create(uint64_t chip, const char *name) {
    return air_nir_create_stage(chip, name, AIR_VERTEX);
}
struct nir_def *air_nir_bits(struct air_nir *s, uint32_t bits, unsigned width) {
    return nir_imm_intN_t(&s->b, bits, width);
}
struct nir_def *air_nir_vertex(struct air_nir *s, unsigned width) {
    nir_def *v = nir_load_vertex_id(&s->b);
    return width == 16 ? nir_iand_imm(&s->b, v, 0xffff) : v;
}
int air_nir_attribute(struct air_nir *s, unsigned location, unsigned components, nir_def **out) {
    if (location >= 16 || !components || components > 4)
        return -1;
    nir_def *v = nir_load_input(&s->b, components, 32, nir_imm_int(&s->b, 0), .base = location,
                                .component = 0, .dest_type = nir_type_float32,
                                .io_semantics = {.location = VERT_ATTRIB_GENERIC0 + location});
    for (unsigned i = 0; i < components; i++)
        out[i] = nir_channel(&s->b, v, i);
    s->b.shader->num_inputs = MAX2(s->b.shader->num_inputs, location + 1);
    return 0;
}
int air_nir_fragment_input(struct air_nir *s, unsigned location, unsigned components, nir_def **out) {
    if (s->b.shader->info.stage != MESA_SHADER_FRAGMENT || location >= 16 || !components || components > 4)
        return -1;
    nir_def *bary = nir_load_barycentric_pixel(&s->b, 32, .interp_mode = INTERP_MODE_SMOOTH);
    nir_def *v = nir_load_interpolated_input(&s->b, components, 32, bary, nir_imm_int(&s->b, 0),
        .base = location, .component = 0, .dest_type = nir_type_float32,
        .io_semantics = {.location = VARYING_SLOT_VAR0 + location, .num_slots = 1});
    for (unsigned i = 0; i < components; i++) out[i] = nir_channel(&s->b, v, i);
    s->b.shader->num_inputs = MAX2(s->b.shader->num_inputs, location + 1);
    return 0;
}
struct nir_def *air_nir_load(struct air_nir *s, unsigned binding, unsigned offset) {
    s->b.shader->info.num_ubos = MAX2(s->b.shader->info.num_ubos, binding + 1);
    return nir_load_ubo(&s->b, 1, 32, nir_imm_int(&s->b, binding), nir_imm_int(&s->b, offset),
                        .align_mul = 4, .align_offset = 0, .range_base = offset, .range = 4);
}
/* Exact for a coefficient in {+0,+1} under the source call's explicit
 * nnan/ninf/nsz contract. The product introduces no rounding. The zero
 * product may return the addend directly because zero sign is immaterial.
 * This does not weaken arbitrary-coefficient FMA or infer flags globally. */
static nir_def *exact_finite_add(nir_builder *b, nir_def *value, nir_def *addend) {
    unsigned saved = b->fp_math_ctrl;
    /* Source flags permit discarding special-value/zero-sign distinctions,
     * but this identity does not authorize another rounding or reassociation. */
    b->fp_math_ctrl = nir_fp_exact;
    nir_def *sum = nir_fadd(b, value, addend);
    b->fp_math_ctrl = saved;
    return sum;
}
static nir_def *finite_binary_coefficient(nir_builder *b, nir_def *coef, nir_def *value,
                                          nir_def *addend) {
    if (coef->bit_size != 32 || coef->num_components != 1)
        return NULL;
    nir_src source = nir_src_for_ssa(coef);
    if (nir_src_is_const(source)) {
        uint32_t bits = nir_src_as_uint(source);
        if (!(bits & 0x7fffffff))
            return addend;
        if (bits == 0x3f800000)
            return exact_finite_add(b, value, addend);
        return NULL;
    }
    if (nir_def_instr(coef)->type != nir_instr_type_alu)
        return NULL;
    nir_alu_instr *alu = nir_instr_as_alu(nir_def_instr(coef));
    if (alu->op != nir_op_bcsel)
        return NULL;
    for (unsigned i = 1; i < 3; i++)
        if (!nir_src_is_const(alu->src[i].src) || alu->src[i].src.ssa->num_components != 1 ||
            alu->src[i].swizzle[0] != 0)
            return NULL;
    uint32_t yes = nir_src_as_uint(alu->src[1].src), no = nir_src_as_uint(alu->src[2].src);
    bool yes_one = yes == 0x3f800000, no_one = no == 0x3f800000;
    if ((!yes_one && (yes & 0x7fffffff)) || (!no_one && (no & 0x7fffffff)))
        return NULL;
    nir_def *sum = exact_finite_add(b, value, addend);
    return nir_bcsel(b, nir_ssa_for_alu_src(b, alu, 0), yes_one ? sum : addend,
                     no_one ? sum : addend);
}
struct nir_def *air_nir_op(struct air_nir *s, enum air_op op, unsigned flags, nir_def *a,
                           nir_def *b, nir_def *c) {
    /* Conservatively preserve every source rounding step. Special-value
     * permissions are taken from this instruction, never module defaults. */
    s->b.fp_math_ctrl = nir_fp_exact;
    if (!(flags & AIR_NNAN))
        s->b.fp_math_ctrl |= nir_fp_preserve_nan;
    if (!(flags & AIR_NINF))
        s->b.fp_math_ctrl |= nir_fp_preserve_inf;
    if (!(flags & AIR_NSZ))
        s->b.fp_math_ctrl |= nir_fp_preserve_signed_zero;
    switch (op) {
    case AIR_EQ:
        return nir_ieq(&s->b, a, b);
    case AIR_UGT:
        return nir_ult(&s->b, b, a);
    case AIR_OR:
        return nir_ior(&s->b, a, b);
    case AIR_AND:
        return nir_iand(&s->b, a, b);
    case AIR_SELECT:
        return nir_bcsel(&s->b, a, b, c);
    case AIR_FADD:
        return nir_fadd(&s->b, a, b);
    case AIR_FSUB:
        return nir_fsub(&s->b, a, b);
    case AIR_FMUL:
        return nir_fmul(&s->b, a, b);
    case AIR_FDIV:
        if (!(flags & AIR_ARCP))
            return NULL;
        s->allow_reciprocal = true;
        return nir_fdiv(&s->b, a, b);
    case AIR_FMA_FINITE_NSZ: {
        if ((flags & (AIR_NNAN | AIR_NINF | AIR_NSZ)) != (AIR_NNAN | AIR_NINF | AIR_NSZ))
            return air_fused_f32(&s->b, a, b, c);
        nir_def *special = finite_binary_coefficient(&s->b, a, b, c);
        if (!special)
            special = finite_binary_coefficient(&s->b, b, a, c);
        if (special)
            return special;
        return air_fused_f32(&s->b, a, b, c);
    }
    case AIR_FSAT:
        if ((flags & (AIR_NNAN | AIR_NINF | AIR_NSZ)) != (AIR_NNAN | AIR_NINF | AIR_NSZ)) return NULL;
        return nir_fsat(&s->b, a);
    case AIR_FMA:
        return air_fused_f32(&s->b, a, b, c);
    }
    abort();
}
int air_nir_store(struct air_nir *s, unsigned slot, unsigned count, nir_def **values) {
    if (s->b.shader->info.stage != MESA_SHADER_VERTEX || slot >= 16 || count < 1 || count > 4)
        return -1;
    nir_def *vector = nir_vec(&s->b, values, count);
    nir_store_output(
        &s->b, vector, nir_imm_int(&s->b, 0), .base = slot, .write_mask = (1u << count) - 1,
        .src_type = nir_type_float32,
        .io_semantics = {.location = slot ? VARYING_SLOT_VAR0 + slot - 1 : VARYING_SLOT_POS});
    s->outputs = MAX2(s->outputs, slot + 1);
    s->b.shader->num_outputs = s->outputs;
    return 0;
}
int air_nir_color(struct air_nir *s, unsigned slot, unsigned target, unsigned count, nir_def **values) {
    if (s->b.shader->info.stage != MESA_SHADER_FRAGMENT || slot >= 8 || target >= 8 || count < 1 || count > 4)
        return -1;
    nir_def *vector = nir_vec(&s->b, values, count);
    nir_store_output(&s->b, vector, nir_imm_int(&s->b, 0), .base = slot,
        .write_mask = (1u << count) - 1, .src_type = nir_type_float32,
        .io_semantics = {.location = FRAG_RESULT_DATA0 + target});
    s->outputs = MAX2(s->outputs, slot + 1);
    s->b.shader->num_outputs = s->outputs;
    return 0;
}
/* Host test only: substitute inputs in a clone and use NIR's constant folder.
 * This is neither the deployment path nor a CPU rendering implementation. */
int air_nir_test_eval_resources(struct air_nir *s, uint32_t vertex, const uint32_t (*attributes)[4],
                                const struct air_nir_test_buffer buffers[16], float *out) {
    nir_shader *clone = nir_shader_clone(NULL, s->b.shader);
    nir_function_impl *impl = nir_shader_get_entrypoint(clone);
    nir_builder b = nir_builder_create(impl);
    int result = -1;
    nir_foreach_block(block, impl) {
        nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
                continue;
            nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
            b.cursor = nir_before_instr(instr);
            nir_def *value;
            if (in->intrinsic == nir_intrinsic_load_vertex_id) {
                value = nir_imm_int(&b, vertex);
            } else if (in->intrinsic == nir_intrinsic_load_ubo) {
                if (!nir_src_is_const(in->src[0]) || nir_src_as_uint(in->src[0]) >= 16 ||
                    !nir_src_is_const(in->src[1]))
                    goto done;
                unsigned binding = nir_src_as_uint(in->src[0]);
                const uint8_t *data = buffers[binding].data;
                unsigned size = buffers[binding].size;
                unsigned offset = nir_src_as_uint(in->src[1]);
                if (!data || size < 4 || offset > size - 4)
                    goto done;
                uint32_t bits;
                memcpy(&bits, data + offset, 4);
                value = nir_imm_int(&b, bits);
            } else if (in->intrinsic == nir_intrinsic_load_barycentric_pixel) {
                if (!attributes || clone->info.stage != MESA_SHADER_FRAGMENT ||
                    nir_intrinsic_interp_mode(in) != INTERP_MODE_SMOOTH) goto done;
                // Test only: the supplied fragment values are already interpolated.
                // The barycentric source is not evaluated as rasterization.
                value = nir_imm_vec2(&b, 0, 0);
            } else if (in->intrinsic == nir_intrinsic_load_input ||
                       in->intrinsic == nir_intrinsic_load_interpolated_input) {
                unsigned slot = nir_intrinsic_base(in), component = nir_intrinsic_component(in);
                if (!attributes || slot >= 16 || component + in->num_components > 4 ||
                    !nir_src_is_const(in->src[in->intrinsic == nir_intrinsic_load_interpolated_input ? 1 : 0]) ||
                    nir_src_as_uint(in->src[in->intrinsic == nir_intrinsic_load_interpolated_input ? 1 : 0]))
                    goto done;
                nir_const_value words[4] = {{0}};
                for (unsigned i = 0; i < in->num_components; i++)
                    words[i].u32 = attributes[slot][component + i];
                value = nir_build_imm(&b, in->num_components, 32, words);
            } else if (in->intrinsic == nir_intrinsic_store_output)
                continue;
            else
                goto done;
            nir_def_rewrite_uses(&in->def, value);
            nir_instr_remove(instr);
        }
    }
    for (unsigned i = 0; i < 16; i++) {
        bool progress = nir_opt_constant_folding(clone);
        progress |= nir_opt_dce(clone);
        if (!progress)
            break;
        if (i == 15)
            goto done;
    }
    unsigned seen = 0;
    nir_foreach_block(block, impl) {
        nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
                continue;
            nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
            if (in->intrinsic != nir_intrinsic_store_output || !nir_src_is_const(in->src[0]))
                goto done;
            unsigned slot = nir_intrinsic_base(in);
            if (slot >= s->outputs || (seen & (1u << slot)))
                goto done;
            seen |= 1u << slot;
            nir_const_value *values = nir_src_as_const_value(in->src[0]);
            for (unsigned i = 0; i < in->num_components; i++)
                out[slot * 4 + i] = values[i].f32;
        }
    }
    if (seen == (1u << s->outputs) - 1)
        result = 0;
done:
    ralloc_free(clone);
    return result;
}
int air_nir_test_eval(struct air_nir *s, uint32_t vertex, const uint8_t *data, unsigned size,
                      float *out) {
    struct air_nir_test_buffer buffers[16] = {{0}};
    buffers[0] = (struct air_nir_test_buffer){data, size};
    return air_nir_test_eval_resources(s, vertex, NULL, buffers, out);
}
int air_nir_lower_integer_ops(struct air_nir *s) {
    for (unsigned i = 0; i < 8; i++) {
        bool lowered = nir_lower_int64(s->b.shader);
        nir_opt_copy_prop(s->b.shader);
        nir_opt_constant_folding(s->b.shader);
        nir_opt_dce(s->b.shader);
        if (!lowered)
            break;
        if (i == 7)
            return -1;
    }
    /* Do not run general floating-point algebraic optimizations here: this
     * stage qualifies integer lowering independently of reciprocal lowering.
     * 64-bit pack/unpack wires may remain until standard backend cleanup. */
    if (nir_lower_int64(s->b.shader))
        return -1;
    nir_validate_shader(s->b.shader, "integer FMA after real NIR int64 lowering");
    return 0;
}
static int write_file(const char *prefix, const char *suffix, const void *data, size_t size) {
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s.%s", prefix, suffix);
    if (length < 0 || (size_t)length >= sizeof(path))
        return -1;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) || !ok ? -1 : 0;
}
static int dump_nir(struct air_nir *s, const char *prefix, const char *suffix) {
    char *text = NULL;
    size_t length = 0;
    FILE *f = open_memstream(&text, &length);
    if (!f)
        return -1;
    nir_print_shader(s->b.shader, f);
    int status = fclose(f);
    if (!status)
        status = write_file(prefix, suffix, text, length);
    free(text);
    return status ? -1 : 0;
}

static bool lower_permitted_div(nir_builder *b, nir_alu_instr *alu, void *data) {
    struct air_nir *s = data;
    if (alu->op != nir_op_fdiv || !s->allow_reciprocal)
        return false;
    b->cursor = nir_before_instr(&alu->instr);
    b->fp_math_ctrl = alu->fp_math_ctrl;
    nir_def *x = nir_ssa_for_alu_src(b, alu, 0), *y = nir_ssa_for_alu_src(b, alu, 1);
    nir_def *result = nir_fmul(b, x, nir_frcp(b, y));
    nir_def_rewrite_uses(&alu->def, result);
    nir_instr_remove(&alu->instr);
    s->reciprocal_lowerings++;
    return true;
}
static int collect_constant_reads(struct air_nir *s) {
    s->read_count = 0;
    nir_foreach_block(block, nir_shader_get_entrypoint(s->b.shader)) {
        nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
                continue;
            nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
            if (in->intrinsic != nir_intrinsic_load_ubo)
                continue;
            if (!nir_src_is_const(in->src[0]) || !nir_src_is_const(in->src[1]) ||
                nir_src_as_uint(in->src[0]) >= AIR_CONSTANT_MAX_BINDINGS ||
                s->read_count >= AIR_CONSTANT_MAX_READS || in->def.bit_size != 32)
                return -1;
            s->reads[s->read_count++] = (struct air_constant_read){
                nir_src_as_uint(in->src[0]), nir_src_as_uint(in->src[1]), in->num_components * 4};
        }
    }
    return 0;
}
static int write_backend_abi(struct air_nir *s, const char *prefix,
                             const struct ir3_shader_variant *v) {
    if (s->compiler->dev_id != &s->dev_id)
        return -1;
    char *text = NULL;
    size_t size = 0;
    FILE *f = open_memstream(&text, &size);
    if (!f)
        return -1;
    int status = air_export_backend_abi(f, v, s->reads, s->read_count);
    if (fclose(f))
        status = -1;
    if (!status)
        status = write_file(prefix, "ir3.json", text, size);
    free(text);
    return status;
}
int air_nir_compile(struct air_nir *s, const char *prefix) {
    if (air_nir_lower_integer_ops(s))
        return -2;
    nir_shader_gather_info(s->b.shader, nir_shader_get_entrypoint(s->b.shader));
    nir_validate_shader(s->b.shader, "original AIR lowered before backend float legalization");
    if (dump_nir(s, prefix, "nir"))
        return -1;
    nir_shader_alu_pass(s->b.shader, lower_permitted_div, nir_metadata_control_flow, s);
    nir_validate_shader(s->b.shader, "explicit source-permitted reciprocal lowering");
    if (dump_nir(s, prefix, "backend.nir"))
        return -1;
    /* Strong FMA must not silently become the backend's unfused MAD. */
    nir_foreach_block(block, nir_shader_get_entrypoint(s->b.shader)) {
        nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_alu && (nir_instr_as_alu(instr)->op == nir_op_ffma ||
                                                      nir_instr_as_alu(instr)->op == nir_op_fdiv)) {
                fprintf(stderr, "AIR backend refused: unlegalized fused FMA or division\n");
                return -2;
            }
        }
    }
    if (collect_constant_reads(s))
        return -2;
    const struct ir3_shader_options options = {.api_wavesize = IR3_SINGLE_OR_DOUBLE,
                                               .real_wavesize = IR3_SINGLE_OR_DOUBLE};
    struct ir3_shader *shader = ir3_shader_from_nir(s->compiler, s->b.shader, &options);
    s->b.shader = NULL; /* consumed by IR3 */
    const struct ir3_shader_key key = {0};
    struct ir3_shader_variant *v = ir3_shader_get_variant(shader, &key, false, true, NULL, NULL);
    int result = -1;
    if (v && v->bin && v->info.sizedwords && v->info.size >= v->info.sizedwords * 4 &&
        v->disasm_info.disasm && !write_backend_abi(s, prefix, v) &&
        !write_file(prefix, "bin", v->bin, v->info.size) &&
        !write_file(prefix, "asm", v->disasm_info.disasm, strlen(v->disasm_info.disasm))) {
        printf("{\"binary_bytes\":%u,\"code_dwords\":%u,\"asm_roundtrip\":true,\"gpu_executed\":"
               "false,\"reciprocal_lowerings\":%u}\n",
               v->info.size, v->info.sizedwords, s->reciprocal_lowerings);
        result = 0;
    }
    ir3_shader_destroy(shader);
    return result;
}
void air_nir_destroy(struct air_nir *s) {
    if (!s)
        return;
    if (s->b.shader)
        ralloc_free(s->b.shader);
    glsl_type_singleton_decref();
    ir3_compiler_destroy(s->compiler);
    free(s);
}
