/* A compiled shader as clarus/shaders.py writes it: the blob embed_shader
   makes, and its size. The three stages are one C type and three Clarus
   types (extern type VertShader and the others in gfx.clarus), so a fragment
   shader where gfx.pipeline wants a vertex one does not compile. */
#ifndef GSC_SHADER_H
#define GSC_SHADER_H

#include "../../src/gfx/gfx_api.h"

typedef struct gsc_shader { const uint8_t *data; int64_t len; } gsc_shader;
typedef gsc_shader VertShader;
typedef gsc_shader FragShader;
typedef gsc_shader CompShader;

/* The layer takes a blob as the bytes embed_shader gives; it only reads them. */
static inline gs_gfx_bytes gsc_shader_bytes(const gsc_shader *s) { return (gs_gfx_bytes){ (uint8_t *)s->data, s->len }; }
static inline gs_gfx_pipeline gsc_gfx_pipeline_of(const VertShader *vs, const FragShader *fs,
                                                  const gs_gfx_pipeline_desc *desc) {
    return gs_gfx_create_pipeline(gsc_shader_bytes(vs), gsc_shader_bytes(fs), desc);
}
static inline gs_gfx_compute_pipeline gsc_gfx_compute_pipeline_of(const CompShader *cs) {
    return gs_gfx_create_compute_pipeline(gsc_shader_bytes(cs));
}

#endif
