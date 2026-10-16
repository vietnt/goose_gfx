/* Shared by the gfx layer's own C files in src/gfx/, never by a program. The
   layer keeps one device, one optional window and one command buffer being
   recorded; everything is main-thread only, as SDL_GPU and windowing are. */

#ifndef GS_GFX_INTERNAL_H
#define GS_GFX_INTERNAL_H

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gfx_api.h"
#include "gfx_blob.h"

/* --- handles ---------------------------------------------------------------
   A table of slots of one kind. A handle is (generation << 20) | index; a
   slot's generation moves on when it is freed, so an old handle to it no
   longer matches. */

#define GFX_INDEX_BITS 20
#define GFX_INDEX_MASK ((1u << GFX_INDEX_BITS) - 1)

typedef struct {
    const char *what;           /* for messages: "buffer", "texture", ... */
    unsigned char *items;
    uint16_t *gens;
    uint8_t *live;
    uint32_t *freelist;
    uint32_t count, cap, nfree, item_size;
} gfx_table;

void *gfx_table_add(gfx_table *t, uint32_t *id);   /* zeroed */
void *gfx_table_find(gfx_table *t, uint32_t id);   /* NULL for 0 or a stale id */
void *gfx_table_get(gfx_table *t, uint32_t id);    /* as find, a misuse if NULL */
void gfx_table_remove(gfx_table *t, uint32_t id);
/* Calls f on every live item, for teardown. */
void gfx_table_each(gfx_table *t, void (*f)(void *item));
void gfx_table_clear(gfx_table *t);

/* --- resources ------------------------------------------------------------ */

typedef struct {
    SDL_GPUBuffer *buf;
    uint32_t size, usage;
} gfx_buffer_slot;

typedef struct {
    SDL_GPUTexture *tex;
    gs_gfx_texture_desc desc;   /* as created: mips resolved, formats Goose's */
    SDL_GPUTextureFormat format;
    uint32_t usage;             /* SDL usage flags */
} gfx_texture_slot;

typedef struct {
    SDL_GPUSampler *smp;
} gfx_sampler_slot;

/* A submit's fence, shared by everything that waits on that submit: the
   readbacks it carried. Freed when the last of them lets go. */
typedef struct {
    SDL_GPUFence *fence;
    int refs;
    bool signaled;
} gfx_fence;

/* A download recorded in the command buffer being recorded: pending until
   that is submitted, then waiting on the submit's fence. */
typedef struct {
    SDL_GPUTransferBuffer *tb;
    uint32_t size;
    gfx_fence *fence;           /* NULL while pending */
    bool failed;                /* its submit failed */
} gfx_readback_slot;

/* What a shader blob says about its stage (src/gfx/gfx_blob.h). */
typedef struct {
    uint8_t stage;
    uint16_t num_samplers, num_storage_textures_ro, num_storage_textures_rw;
    uint16_t num_storage_buffers_ro, num_storage_buffers_rw, num_uniform_buffers;
    uint32_t uniform_sizes[4];
    uint32_t local_size[3];
    int num_inputs;
    gs_gfx_blob_input inputs[16];
} gfx_shader_info;

/* A compiled SDL shader, shared by every pipeline made from the same blob
   and kept until close(). */
typedef struct {
    uint64_t hash;
    uint32_t size;
    SDL_GPUShader *shader;
    gfx_shader_info info;
} gfx_shader;

/* A pipeline is created for the targets of the pass it is used in, which
   its description does not name: one variant per combination seen. */
typedef struct {
    int ncolor;
    SDL_GPUTextureFormat color[4], depth;
    bool has_depth;
    SDL_GPUSampleCount samples;
} gfx_targets;

typedef struct {
    gfx_targets targets;
    SDL_GPUGraphicsPipeline *p;
} gfx_variant;

#define GFX_MAX_VARIANTS 8

typedef struct {
    gfx_shader *vs, *fs;
    gs_gfx_pipeline_desc desc;
    SDL_GPUVertexAttribute attrs[16];
    int nattrs;
    SDL_GPUVertexBufferDescription vbufs[2];
    int nvbufs;
    uint32_t vbuf_mask;         /* which vertex buffer slots a draw needs */
    int vbuf_from[2];           /* the bind_vertex_buffer slot each of vbufs reads */
    gfx_variant variants[GFX_MAX_VARIANTS];
    int nvariants;
} gfx_pipeline_slot;

typedef struct {
    SDL_GPUComputePipeline *p;
    gfx_shader_info info;
} gfx_compute_slot;

/* --- the one state -------------------------------------------------------- */

enum { GFX_STAGES = 3 };

typedef struct {
    SDL_GPUDevice *dev;
    SDL_Window *window;
    bool video_inited;
    SDL_GPUShaderFormat format;         /* what shaders are handed over as */
    int64_t flags;

    /* The screen: what a frame draws into, blitted to the window when it
       ends. Its handles stay the same when a resize replaces the textures. */
    gs_gfx_texture screen, screen_depth;
    int width, height;

    /* Recording. Uploads and readbacks use command buffers of their own. */
    SDL_GPUCommandBuffer *cmd;
    SDL_GPURenderPass *pass;
    SDL_GPUComputePass *cpass;
    gfx_targets targets;                /* of the render pass */
    gfx_pipeline_slot *pipeline;        /* bound in the render pass */
    gfx_compute_slot *compute;          /* bound in the compute pass */
    /* Bound slots, by stage, since the pass began; checked at a draw. */
    uint32_t samplers_bound[GFX_STAGES], textures_bound[GFX_STAGES], buffers_bound[GFX_STAGES];
    uint32_t vbufs_bound;
    /* Vertex buffers by bind_vertex_buffer slot, and what SDL's own slots
       hold, which a draw makes agree with its pipeline (vbuf_from). */
    SDL_GPUBufferBinding vbufs[2], sdl_vbufs[2];
    bool index_bound;
    int compute_rw_textures, compute_rw_buffers;
    /* Uniform slots pushed, by stage, in this command buffer. */
    uint32_t uniforms_pushed[GFX_STAGES];

    /* Input: held state plus every transition seen in this frame. A tap
       between frames sets both pressed and released, even though it ends released. */
    bool quit;
    uint8_t keys[SDL_SCANCODE_COUNT];
    uint8_t pressed_keys[SDL_SCANCODE_COUNT], released_keys[SDL_SCANCODE_COUNT];
    uint32_t buttons, pressed_buttons, released_buttons;
    float mouse_x, mouse_y, mouse_dx, mouse_dy, wheel;
    bool mouse_in;                      /* over the window, as SDL's mouse focus says */
    bool focused;                       /* keyboard focus; true initially headless */
    bool mouse_relative;                /* explicitly released on focus loss */
    bool cursor_visible;                /* preference outside relative mouse mode */
    uint8_t button_clicks[32];           /* click count to preserve on focus-loss releases */
    /* The window's size in its own coordinates, which SDL gives the mouse
       in, and in pixels: a high-density display has more of the second. */
    int window_w, window_h, window_pw, window_ph;
    /* This frame's events in order, for gs_gfx_events. */
    gs_gfx_event *events;
    int64_t nevents, events_cap;
    /* Text injected and not yet seen: a user event of type inject_event
       carries its index, so it comes in order with injected keys. */
    char **injected;
    int ninjected, injected_cap;
    uint32_t inject_event;
    /* The clipboard without a window: no one else is at it. */
    char *clipboard;

    uint64_t start_ns, last_ns;
    double delta;
    int64_t frames;

    char error[1024];
    int64_t misuse;

    gfx_table buffers, textures, samplers, pipelines, computes, readbacks;
    int readbacks_pending;              /* recorded and not yet submitted */
    /* The fences of frame submits the GPU may not have done, oldest first,
       and how many there were, its own included, when the last frame()
       submitted. */
    gfx_fence *in_flight[2];
    int nin_flight;
    int64_t frames_in_flight;
    gfx_shader **shaders;
    int nshaders, shaders_cap;

    /* D3DCompile, loaded from d3dcompiler_47.dll, for DXBC. */
    void *d3dcompiler;
    void *d3dcompile;
} gfx_state;

extern gfx_state gfx;

/* --- errors --------------------------------------------------------------- */

/* Something outside the program's control failed: the reason is kept for
   gs_gfx_error. Returns false, for `return gfx_fail(...)`. */
bool gfx_fail(const char *fmt, ...);
/* The program used the API wrongly: kept, counted and printed, and the
   Goose side aborts on it at the next frame. Returns false. */
bool gfx_misuse(const char *fmt, ...);
/* A copy of SDL's reason for the failure of `what`. */
bool gfx_sdl_fail(const char *what);

/* True inside open() .. close(); a misuse to call `what` otherwise. */
bool gfx_need_device(const char *what);

/* --- shared helpers ------------------------------------------------------- */

/* The command buffer being recorded, acquired on first use. */
SDL_GPUCommandBuffer *gfx_cmd(void);
/* Submits what has been recorded, if anything, without waiting. */
bool gfx_submit(void);
/* Submits what has been recorded and waits until the GPU has done it all. */
bool gfx_finish(void);
/* Submits what has been recorded (an empty command buffer if nothing),
   with a fence that the readbacks still pending now share; the caller
   holds one reference. NULL if the submit failed, which fails them. */
gfx_fence *gfx_submit_fenced(void);
bool gfx_fence_wait(gfx_fence *f);
bool gfx_fence_done(gfx_fence *f);
void gfx_fence_drop(gfx_fence *f);

/* The Goose format constants, mapped. */
bool gfx_texture_format(int32_t format, SDL_GPUTextureFormat *out);
const char *gfx_stage_name(int64_t stage);

gfx_texture_slot *gfx_texture_slot_of(gs_gfx_texture t);
gfx_buffer_slot *gfx_buffer_slot_of(gs_gfx_buffer b);
bool gfx_create_screen(int w, int h);

/* Whether HLSL can be turned into DXBC here: d3dcompiler_47.dll loaded. */
bool gfx_have_dxbc(void);
/* The SDL pipeline for drawing `p` into targets `t`, created on first use. */
SDL_GPUGraphicsPipeline *gfx_pipeline_for(gfx_pipeline_slot *p, const gfx_targets *t);

/* Reads a blob's header and reflection; a misuse if it is not one. */
bool gfx_read_blob(gs_gfx_bytes blob, gs_gfx_blob_header *h, gfx_shader_info *info);
/* The SDL shader for a graphics stage's blob, created once per blob. */
gfx_shader *gfx_get_shader(gs_gfx_bytes blob, int stage);
/* The shader code for the device's format: SPIR-V words, MSL text or DXBC. */
bool gfx_shader_code(gs_gfx_bytes blob, const gs_gfx_blob_header *h, void **code, size_t *size,
                     const char **entry);

void gfx_release_resources(void);
void gfx_release_pipelines(void);

#endif
