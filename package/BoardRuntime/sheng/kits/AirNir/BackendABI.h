/* SPDX-License-Identifier: MIT */
#ifndef AIR_BACKEND_ABI_H
#define AIR_BACKEND_ABI_H
#include "ConstantUploads.h"
#include "ir3/ir3_shader.h"
#include <inttypes.h>

static void air_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\')
            fprintf(f, "\\%c", *p);
        else if (*p < 32)
            fprintf(f, "\\u%04x", *p);
        else
            fputc(*p, f);
    }
    fputc('"', f);
}
static const char *air_allocation_name(unsigned i) {
    const char *names[] = {"push_consts",
                           "dynamic_descriptor_offsets",
                           "inline_uniform_addresses",
                           "driver_params",
                           "ubo_ranges",
                           "preamble",
                           "global",
                           "ubo_pointers",
                           "image_dimensions",
                           "transform_feedback",
                           "primitive_params",
                           "primitive_map",
                           "bindless_base_addresses"};
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "unknown";
}
static int air_export_backend_abi(FILE *f, const struct ir3_shader_variant *v,
                                  const struct air_constant_read *reads, unsigned count) {
    const struct ir3_const_state *cs = ir3_const_state(v);
    const struct ir3_compiler *c = v->compiler;
    if ((v->type != MESA_SHADER_VERTEX && v->type != MESA_SHADER_FRAGMENT) || v->inputs_count > 34 || v->outputs_count > 34 ||
        cs->ubo_state.num_enabled > AIR_CONSTANT_MAX_RANGES || count > AIR_CONSTANT_MAX_READS ||
        v->imm_state.size > AIR_CONSTANT_MAX_BYTES / 4 ||
        (v->imm_state.size && !v->imm_state.values) || v->constlen > AIR_CONSTANT_MAX_BYTES / 16)
        return -1;
    fprintf(f,
            "{\n\"format_version\":1,\"stage\":\"%s\",\"generation\":%u,\"gpu_name\":",
            v->type == MESA_SHADER_VERTEX ? "vertex" : "fragment", c->gen);
    air_json_string(f, fd_dev_name(c->dev_id));
    fprintf(f,
            ",\"chip_id\":\"0x%" PRIx64 "\",\"binary_bytes\":%u,\"code_dwords\":%u,"
            "\"binary_alignment_bytes\":%u,\"instruction_groups\":%u,\"constant_bytes\":%u,"
            "\"upload_alignment_bytes\":%u,\"max_full_register\":%d,\"max_half_register\":%d,"
            "\"branch_stack\":%u,\"private_bytes_per_fiber\":%u,\"private_per_wave\":%s,"
            "\"subgroup_size\":%u,\"double_threadsize\":%s,\"early_preamble\":%s,"
            "\"main_instructions\":%u,\"preamble_instructions\":%u,"
            "\"constant_data_offset\":%u,\"constant_data_bytes\":%u,"
            "\"app_ubo_slots\":%u,\"remaining_ubo_slots\":%u,\"driver_parameter_words\":%u,"
            "\"driver_params_ubo\":%d,\"primitive_params_ubo\":%d,\"primitive_map_ubo\":%d,"
            "\"push_ubo_with_preamble\":%s,\"load_shader_consts_via_preamble\":%s,"
            "\"input_attribute_slots\":%u,\"output_words\":%u,\"writes_position\":%s,"
            "\"runtime_binding_qualified\":false,\"gpu_executed\":false,\n",
            c->dev_id->chip_id, v->info.size, v->info.sizedwords, c->instr_align * 8, v->instrlen,
            v->constlen * 16, c->const_upload_unit * 16, v->info.max_reg, v->info.max_half_reg,
            v->branchstack, v->pvtmem_size, v->pvtmem_per_wave ? "true" : "false",
            v->info.subgroup_size, v->info.double_threadsize ? "true" : "false",
            v->early_preamble ? "true" : "false", v->info.instrs_count,
            v->info.preamble_instrs_count, v->info.constant_data_offset, v->constant_data_size,
            cs->num_app_ubos, cs->num_ubos, cs->num_driver_params, cs->driver_params_ubo.idx,
            cs->primitive_param_ubo.idx, cs->primitive_map_ubo.idx,
            c->options.push_ubo_with_preamble ? "true" : "false",
            c->info->props.load_shader_consts_via_preamble ? "true" : "false", v->attr_in,
            v->output_size, v->writes_pos ? "true" : "false");
    fprintf(f,
        "\"binding_state\":{\"branch_stack_hw\":%u,\"merged_registers\":%s,"
        "\"input_words\":%u,\"need_full_quad\":%s,\"need_pixel_lod\":%s},\n",
        ir3_shader_branchstack_hw(v), v->mergedregs ? "true" : "false", v->total_in,
        v->need_full_quad ? "true" : "false", v->need_pixlod ? "true" : "false");
    fprintf(
        f,
        "\"constant_limits_vec4\":{\"pipeline\":%u,\"geometry\":%u,\"fragment\":%u,\"safe\":%u},\n",
        c->max_const_pipeline, c->max_const_geom, c->max_const_frag, c->max_const_safe);
    fprintf(f, "\"allocations\":[");
    for (unsigned i = 0; i < IR3_CONST_ALLOC_MAX; i++) {
        if (i)
            fputc(',', f);
        const struct ir3_const_allocation *a = &cs->allocs.consts[i];
        fprintf(f,
                "{\"type\":%u,\"name\":\"%s\",\"begin\":%u,\"bytes\":%u,\"reserved_vec4\":%u,"
                "\"reserved_alignment_vec4\":%u}",
                i, air_allocation_name(i), a->offset_vec4 * 16, a->size_vec4 * 16,
                a->reserved_size_vec4, a->reserved_align_vec4);
    }
    fprintf(f, "],\n\"ubo_ranges\":[");
    for (unsigned i = 0; i < cs->ubo_state.num_enabled; i++) {
        if (i)
            fputc(',', f);
        const struct ir3_ubo_range *r = &cs->ubo_state.range[i];
        fprintf(f,
                "{\"binding\":%u,\"source_begin\":%u,\"source_end\":%u,\"destination_begin\":%u,"
                "\"bindless\":%s,\"global\":%s,\"descriptor_set\":%u}",
                r->ubo.block, r->start, r->end, r->offset, r->ubo.bindless ? "true" : "false",
                r->ubo.global ? "true" : "false", r->ubo.bindless_base);
    }
    fprintf(f, "],\n\"source_reads\":[");
    for (unsigned i = 0; i < count; i++) {
        if (i)
            fputc(',', f);
        fprintf(f, "{\"binding\":%u,\"offset\":%u,\"bytes\":%u}", reads[i].binding, reads[i].offset,
                reads[i].bytes);
    }
    fprintf(f,
            "],\n\"immediates\":{\"destination_begin\":%u,\"logical_words\":%u,\"padded_words\":%u,"
            "\"cpu_upload\":%s,\"values\":[",
            cs->allocs.max_const_offset_vec4 * 16, v->imm_state.count, v->imm_state.size,
            c->info->props.load_shader_consts_via_preamble ? "false" : "true");
    for (unsigned i = 0; i < v->imm_state.size; i++) {
        if (i)
            fputc(',', f);
        fprintf(f, "%u", v->imm_state.values[i]);
    }
    fprintf(f, "]},\n\"inputs\":[");
    for (unsigned i = 0; i < v->inputs_count; i++) {
        if (i)
            fputc(',', f);
        fprintf(f,
                "{\"slot\":%u,\"register_id\":%u,\"component_mask\":%u,\"inloc\":%u,\"system_"
                "value\":%s,\"half\":%s}",
                v->inputs[i].slot, v->inputs[i].regid, v->inputs[i].compmask, v->inputs[i].inloc,
                v->inputs[i].sysval ? "true" : "false", v->inputs[i].half ? "true" : "false");
    }
    fprintf(f, "],\n\"outputs\":[");
    for (unsigned i = 0; i < v->outputs_count; i++) {
        if (i)
            fputc(',', f);
        fprintf(
            f, "{\"slot\":%u,\"register_id\":%u,\"view\":%u,\"aliased_components\":%u,\"half\":%s}",
            v->outputs[i].slot, v->outputs[i].regid, v->outputs[i].view,
            v->outputs[i].aliased_components, v->outputs[i].half ? "true" : "false");
    }
    fprintf(f, "]\n}\n");
    return ferror(f) ? -1 : 0;
}
#endif
