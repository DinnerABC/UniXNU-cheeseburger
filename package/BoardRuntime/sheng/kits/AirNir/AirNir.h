/* SPDX-License-Identifier: MIT */
#ifndef AIR_NIR_H
#define AIR_NIR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct air_nir;
struct nir_def;
enum air_op {
    AIR_EQ,
    AIR_UGT,
    AIR_OR,
    AIR_AND,
    AIR_SELECT,
    AIR_FADD,
    AIR_FSUB,
    AIR_FMUL,
    AIR_FDIV,
    AIR_FMA,
    AIR_FMA_FINITE_NSZ,
    AIR_FSAT
};
enum air_fp_flag { AIR_NNAN = 1, AIR_NINF = 2, AIR_NSZ = 4, AIR_ARCP = 8 };
enum air_stage { AIR_VERTEX, AIR_FRAGMENT };
struct air_nir *air_nir_create_stage(uint64_t chip, const char *name, enum air_stage);
struct air_nir *air_nir_create(uint64_t chip, const char *name);
struct nir_def *air_nir_bits(struct air_nir *, uint32_t bits, unsigned width);
struct nir_def *air_nir_vertex(struct air_nir *, unsigned width);
int air_nir_fragment_input(struct air_nir *, unsigned location, unsigned components, struct nir_def **);
int air_nir_attribute(struct air_nir *, unsigned location, unsigned components, struct nir_def **);
struct nir_def *air_nir_load(struct air_nir *, unsigned binding, unsigned offset);
struct nir_def *air_nir_op(struct air_nir *, enum air_op, unsigned fp_flags, struct nir_def *,
                           struct nir_def *, struct nir_def *);
int air_nir_store(struct air_nir *, unsigned slot, unsigned count, struct nir_def **);
int air_nir_color(struct air_nir *, unsigned slot, unsigned target, unsigned count, struct nir_def **);
int air_nir_test_eval(struct air_nir *, uint32_t vertex, const uint8_t *data, unsigned size,
                      float *out);
struct air_nir_test_buffer {
    const uint8_t *data;
    unsigned size;
};
int air_nir_test_eval_resources(struct air_nir *, uint32_t vertex, const uint32_t (*attributes)[4],
                                const struct air_nir_test_buffer buffers[16], float *out);
int air_nir_lower_integer_ops(struct air_nir *);
int air_nir_compile(struct air_nir *, const char *prefix);
void air_nir_destroy(struct air_nir *);
#ifdef __cplusplus
}
#endif
#endif
