/* The gfx layer's C API: every function stdlib/gfx.goose reaches through an
   `extern "gs_gfx_..." fn`, listed once in GS_GFX_API. That list expands into
   the prototypes below, into the symbol table a JIT run registers
   (src/jit.h), and the test suite checks stdlib/gfx.goose against it.

   What crosses is what `extern fn` can pass (spec 7.10): scalars, `u8[:]`
   and slices of flat structs as { data, len }, flat structs by value, and T&
   as a pointer. A bool is a uint8_t. Goose structs are packed, so every
   struct here is packed too, and each is declared again in stdlib/gfx.goose
   with the same fields in the same order: the Goose struct `TextureDesc` is
   `gs_gfx_texture_desc` here, a slice of `Buffer` is `gs_gfx_buffer_slice`.

   Handles are one u32: a slot index in the low 20 bits and a generation
   above it, so a released or stale handle is an error, not a crash. 0 is
   never a valid handle.

   Errors: a function that can fail for reasons outside the program (no GPU,
   a file that is not there) returns false or a zero handle, with the reason
   in gs_gfx_error. A call the program should not have made (drawing with no
   pipeline bound, a stale handle) is also counted by gs_gfx_misuse_count,
   which the Goose side checks every frame and turns into an abort. */

#ifndef GS_GFX_API_H
#define GS_GFX_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

typedef struct { uint8_t *data; int64_t len; } gs_gfx_bytes;   /* u8[:], the generated sl_u8 */

typedef struct { uint32_t id; } gs_gfx_buffer;
typedef struct { uint32_t id; } gs_gfx_texture;
typedef struct { uint32_t id; } gs_gfx_sampler;
typedef struct { uint32_t id; } gs_gfx_pipeline;
typedef struct { uint32_t id; } gs_gfx_compute_pipeline;
typedef struct { uint32_t id; } gs_gfx_readback;

/* One input event of a frame, in the order they came: the fields a kind has
   are the ones that mean something for it. */
typedef struct {
    int32_t kind;               /* EVENT_* */
    int32_t scancode;           /* key: where it is on the keyboard, as key names name it */
    int32_t keycode;            /* key: what it means in the keyboard's layout (SDL_Keycode) */
    int32_t mods;               /* key: the MOD_* held down with it */
    uint8_t down;               /* key, button: pressed rather than released */
    uint8_t repeat;             /* key: repeated by being held down */
    int32_t button;             /* button: MOUSE_* */
    int32_t clicks;             /* button: 1, 2 for a double click, ... */
    uint32_t codepoint;         /* text: one character typed */
    float x, y;                 /* motion, button: where the pointer is; wheel: how far */
} gs_gfx_event;

typedef struct { gs_gfx_buffer *data; int64_t len; } gs_gfx_buffer_slice;
typedef struct { gs_gfx_event *data; int64_t len; } gs_gfx_event_slice;
typedef struct { gs_gfx_texture *data; int64_t len; } gs_gfx_texture_slice;

typedef struct { int32_t x, y; } gs_gfx_int2;
typedef struct { float x, y; } gs_gfx_float2;
typedef struct { float x, y, z, w; } gs_gfx_float4;

/* What SDL can tell of the GPU's memory: architecture is a MEMORY_UNIFIED or
   MEMORY_DEDICATED, 0 when unknown; bytes is a MEMORY_BUDGET or
   MEMORY_CAPACITY as kind says, 0 bytes with kind 0. */
typedef struct {
    int32_t architecture, kind;
    int64_t bytes;
} gs_gfx_memory_info;

typedef struct {
    int32_t kind, format, usage;
    int32_t width, height;
    int32_t depth;              /* a 3D texture's depth, or an array's layers */
    int32_t mips;               /* 0: the full chain */
    int32_t samples;
} gs_gfx_texture_desc;

/* Part of one mip of one layer. w == 0 means the whole mip. */
typedef struct {
    int32_t mip, layer;
    int32_t x, y, z;
    int32_t w, h, d;
} gs_gfx_region;

typedef struct {
    int32_t min_filter, mag_filter, mip_filter;
    int32_t wrap_u, wrap_v, wrap_w;
    float max_anisotropy;       /* 1 or less: off */
    int32_t compare;            /* a COMPARE_*, for a shadow sampler; 0: none */
    float min_lod, max_lod;     /* the mip levels sampling may reach, clamped to the texture's */
    float lod_bias;             /* added to the level of detail before the clamp; not on Metal */
} gs_gfx_sampler_desc;

typedef struct {
    int32_t primitive, cull;
    uint8_t clockwise;          /* front faces wind clockwise */
    uint8_t wireframe, depth_test, depth_write;
    int32_t depth_compare, blend;
    /* Vertex inputs at this location and above come from vertex buffer 1,
       per instance; -1: none do. */
    int32_t instance_location;
    float depth_bias, depth_bias_slope;
    /* Per location, the attribute format a vertex buffer supplies; 0: the
       shader input's own type, as f32/i32/u32 components. */
    uint8_t formats[16];
    /* With blend BLEND_CUSTOM: SDL_GPUBlendFactor and SDL_GPUBlendOp values
       for color and for alpha. */
    int32_t blend_src, blend_dst, blend_op, blend_src_alpha, blend_dst_alpha, blend_op_alpha;
    int32_t write_mask;         /* the WRITE_* channels a draw writes, of every target */
    /* Stencil: SDL_GPUCompareOp and SDL_GPUStencilOp values for front faces,
       and for back faces where stencil_back_compare is not 0. */
    uint8_t stencil_test;
    int32_t stencil_compare, stencil_fail, stencil_depth_fail, stencil_pass;
    int32_t stencil_back_compare, stencil_back_fail, stencil_back_depth_fail, stencil_back_pass;
    int32_t stencil_read_mask, stencil_write_mask;
} gs_gfx_pipeline_desc;

typedef struct {
    gs_gfx_texture color[4];    /* the targets, up to the first 0 */
    gs_gfx_texture resolve[4];  /* for a multisampled target, where it resolves to */
    gs_gfx_texture depth;       /* 0: none */
    int32_t layer, mip;         /* of the color targets */
    uint8_t clear_color, clear_depth;
    gs_gfx_float4 color_value;
    float depth_value;
    uint8_t clear_stencil, stencil_value;
} gs_gfx_pass_desc;

#pragma pack(pop)

#define GS_GFX_API(X) \
    /* Device, window and frames. */ \
    X(uint8_t, gs_gfx_available, (void)) \
    X(uint8_t, gs_gfx_open, (gs_gfx_bytes title, int64_t width, int64_t height, int64_t flags)) \
    X(uint8_t, gs_gfx_open_headless, (int64_t width, int64_t height, int64_t flags)) \
    X(void, gs_gfx_close, (void)) \
    X(int64_t, gs_gfx_error, (gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_misuse_count, (void)) \
    X(int64_t, gs_gfx_driver, (gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_present_mode, (void)) \
    X(uint8_t, gs_gfx_gpu_memory, (gs_gfx_memory_info *out)) \
    X(uint8_t, gs_gfx_frame, (void)) \
    X(void, gs_gfx_quit, (void)) \
    X(void, gs_gfx_flush, (void)) \
    X(void, gs_gfx_set_title, (gs_gfx_bytes title)) \
    X(void, gs_gfx_screen_size, (gs_gfx_int2 *out)) \
    X(gs_gfx_texture, gs_gfx_screen, (void)) \
    X(gs_gfx_texture, gs_gfx_screen_depth, (void)) \
    X(double, gs_gfx_time, (void)) \
    X(double, gs_gfx_delta_time, (void)) \
    X(int64_t, gs_gfx_frame_count, (void)) \
    X(int64_t, gs_gfx_frames_in_flight, (void)) \
    X(double, gs_gfx_gpu_time, (void)) \
    X(int64_t, gs_gfx_open_count, (void)) \
    /* Input. */ \
    X(uint8_t, gs_gfx_key_down, (gs_gfx_bytes name)) \
    X(uint8_t, gs_gfx_key_pressed, (gs_gfx_bytes name)) \
    X(uint8_t, gs_gfx_key_released, (gs_gfx_bytes name)) \
    X(uint8_t, gs_gfx_mouse_down, (int64_t button)) \
    X(uint8_t, gs_gfx_mouse_pressed, (int64_t button)) \
    X(uint8_t, gs_gfx_mouse_released, (int64_t button)) \
    X(void, gs_gfx_mouse_pos, (gs_gfx_float2 *out)) \
    X(void, gs_gfx_mouse_delta, (gs_gfx_float2 *out)) \
    X(float, gs_gfx_mouse_wheel, (void)) \
    X(uint8_t, gs_gfx_mouse_in_window, (void)) \
    X(uint8_t, gs_gfx_focused, (void)) \
    X(uint8_t, gs_gfx_set_mouse_relative, (uint8_t on)) \
    X(uint8_t, gs_gfx_mouse_relative, (void)) \
    X(uint8_t, gs_gfx_set_cursor_visible, (uint8_t visible)) \
    X(uint8_t, gs_gfx_cursor_visible, (void)) \
    X(uint8_t, gs_gfx_inject_focus, (uint8_t focused)) \
    X(uint8_t, gs_gfx_inject_key, (gs_gfx_bytes name, uint8_t down)) \
    X(void, gs_gfx_inject_mouse, (float x, float y, int64_t button, uint8_t down)) \
    X(int64_t, gs_gfx_events, (gs_gfx_event_slice out)) \
    X(void, gs_gfx_text_input, (uint8_t on)) \
    X(uint8_t, gs_gfx_inject_text, (gs_gfx_bytes text)) \
    X(int64_t, gs_gfx_scancode, (gs_gfx_bytes name)) \
    X(int64_t, gs_gfx_key_name, (int64_t scancode, gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_clipboard, (gs_gfx_bytes out)) \
    X(uint8_t, gs_gfx_set_clipboard, (gs_gfx_bytes text)) \
    /* Buffers. */ \
    X(gs_gfx_buffer, gs_gfx_create_buffer, (int64_t usage, int64_t size, gs_gfx_bytes data)) \
    X(uint8_t, gs_gfx_update_buffer, (gs_gfx_buffer b, int64_t offset, gs_gfx_bytes data)) \
    X(uint8_t, gs_gfx_read_buffer, (gs_gfx_buffer b, int64_t offset, gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_buffer_size, (gs_gfx_buffer b)) \
    X(void, gs_gfx_release_buffer, (gs_gfx_buffer b)) \
    X(void, gs_gfx_set_buffer_name, (gs_gfx_buffer b, gs_gfx_bytes name)) \
    /* Textures and samplers. */ \
    X(gs_gfx_texture, gs_gfx_create_texture, (const gs_gfx_texture_desc *desc)) \
    X(gs_gfx_texture, gs_gfx_load_texture, (gs_gfx_bytes path, int64_t flags)) \
    X(uint8_t, gs_gfx_update_texture, (gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes data)) \
    X(uint8_t, gs_gfx_copy_buffer_to_texture, (gs_gfx_buffer b, int64_t offset, gs_gfx_texture t, const gs_gfx_region *region)) \
    X(uint8_t, gs_gfx_read_texture, (gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_region_size, (gs_gfx_texture t, const gs_gfx_region *region)) \
    /* Readbacks that do not wait. */ \
    X(gs_gfx_readback, gs_gfx_read_texture_async, (gs_gfx_texture t, const gs_gfx_region *region)) \
    X(gs_gfx_readback, gs_gfx_read_buffer_async, (gs_gfx_buffer b, int64_t offset, int64_t size)) \
    X(uint8_t, gs_gfx_readback_ready, (gs_gfx_readback r)) \
    X(int64_t, gs_gfx_readback_size, (gs_gfx_readback r)) \
    X(uint8_t, gs_gfx_readback_take, (gs_gfx_readback r, gs_gfx_bytes out)) \
    X(void, gs_gfx_release_readback, (gs_gfx_readback r)) \
    X(uint8_t, gs_gfx_save_png, (gs_gfx_texture t, gs_gfx_bytes path)) \
    X(void, gs_gfx_generate_mips, (gs_gfx_texture t)) \
    X(uint8_t, gs_gfx_texture_info, (gs_gfx_texture t, gs_gfx_texture_desc *out)) \
    X(void, gs_gfx_release_texture, (gs_gfx_texture t)) \
    X(void, gs_gfx_set_texture_name, (gs_gfx_texture t, gs_gfx_bytes name)) \
    X(gs_gfx_sampler, gs_gfx_create_sampler, (const gs_gfx_sampler_desc *desc)) \
    X(void, gs_gfx_release_sampler, (gs_gfx_sampler s)) \
    /* Pipelines. */ \
    X(gs_gfx_pipeline, gs_gfx_create_pipeline, (gs_gfx_bytes vs, gs_gfx_bytes fs, const gs_gfx_pipeline_desc *desc)) \
    X(void, gs_gfx_release_pipeline, (gs_gfx_pipeline p)) \
    X(gs_gfx_compute_pipeline, gs_gfx_create_compute_pipeline, (gs_gfx_bytes cs)) \
    X(void, gs_gfx_release_compute_pipeline, (gs_gfx_compute_pipeline p)) \
    /* Render passes and drawing. */ \
    X(uint8_t, gs_gfx_begin_pass, (const gs_gfx_pass_desc *desc)) \
    X(void, gs_gfx_end_pass, (void)) \
    X(void, gs_gfx_bind_pipeline, (gs_gfx_pipeline p)) \
    X(void, gs_gfx_bind_vertex_buffer, (int64_t slot, gs_gfx_buffer b, int64_t offset)) \
    X(void, gs_gfx_bind_index_buffer, (gs_gfx_buffer b, int64_t index_size, int64_t offset)) \
    X(void, gs_gfx_bind_texture, (int64_t stage, int64_t slot, gs_gfx_texture t, gs_gfx_sampler s)) \
    X(void, gs_gfx_bind_storage_texture, (int64_t stage, int64_t slot, gs_gfx_texture t)) \
    X(void, gs_gfx_bind_storage_buffer, (int64_t stage, int64_t slot, gs_gfx_buffer b)) \
    X(void, gs_gfx_push_uniforms, (int64_t stage, int64_t slot, gs_gfx_bytes data)) \
    X(void, gs_gfx_viewport, (float x, float y, float w, float h)) \
    X(void, gs_gfx_scissor, (int64_t x, int64_t y, int64_t w, int64_t h)) \
    X(void, gs_gfx_blend_constant, (float r, float g, float b, float a)) \
    X(void, gs_gfx_stencil_ref, (int64_t ref)) \
    X(void, gs_gfx_push_group, (gs_gfx_bytes name)) \
    X(void, gs_gfx_pop_group, (void)) \
    X(void, gs_gfx_label, (gs_gfx_bytes text)) \
    X(void, gs_gfx_draw, (int64_t vertices, int64_t instances, int64_t first_vertex, int64_t first_instance)) \
    X(void, gs_gfx_draw_indexed, (int64_t indices, int64_t instances, int64_t first_index, int64_t vertex_offset, int64_t first_instance)) \
    X(void, gs_gfx_draw_indirect, (gs_gfx_buffer b, int64_t offset, int64_t count)) \
    X(void, gs_gfx_draw_indexed_indirect, (gs_gfx_buffer b, int64_t offset, int64_t count)) \
    /* Compute. */ \
    X(uint8_t, gs_gfx_begin_compute, (gs_gfx_buffer_slice rw_buffers, gs_gfx_texture_slice rw_textures, uint8_t cycle)) \
    X(void, gs_gfx_end_compute, (void)) \
    X(void, gs_gfx_bind_compute_pipeline, (gs_gfx_compute_pipeline p)) \
    X(void, gs_gfx_dispatch, (int64_t x, int64_t y, int64_t z)) \
    X(void, gs_gfx_dispatch_indirect, (gs_gfx_buffer b, int64_t offset))

#define GS_GFX_PROTO(ret, name, params) ret name params;
GS_GFX_API(GS_GFX_PROTO)
#undef GS_GFX_PROTO

/* The constants stdlib/gfx.goose declares, with their Goose types; here they
   are GS_GFX_<name>. Where the layer passes one straight through to SDL, it
   has SDL's value, which SDL keeps stable. */
#define GS_GFX_CONSTANTS(X) \
    /* open() and open_headless() flags. */ \
    X(i32, WINDOW_RESIZABLE, 1) \
    X(i32, WINDOW_HIDDEN, 2) \
    X(i32, WINDOW_FULLSCREEN, 4) \
    X(i32, NO_VSYNC, 16) \
    X(i32, DEBUG, 32) \
    X(i32, IMMEDIATE, 64) \
    /* present_mode(): SDL_GPUPresentMode. */ \
    X(i32, PRESENT_VSYNC, 0) \
    X(i32, PRESENT_IMMEDIATE, 1) \
    X(i32, PRESENT_MAILBOX, 2) \
    X(i32, MEMORY_UNIFIED, 1) \
    X(i32, MEMORY_DEDICATED, 2) \
    X(i32, MEMORY_BUDGET, 1) \
    X(i32, MEMORY_CAPACITY, 2) \
    /* Shader stages. */ \
    X(i32, VERTEX, 0) \
    X(i32, FRAGMENT, 1) \
    X(i32, COMPUTE, 2) \
    /* Buffer usage, SDL_GPUBufferUsageFlags. */ \
    X(i32, BUFFER_VERTEX, 1) \
    X(i32, BUFFER_INDEX, 2) \
    X(i32, BUFFER_INDIRECT, 4) \
    X(i32, BUFFER_STORAGE, 8) \
    X(i32, BUFFER_COMPUTE_READ, 16) \
    X(i32, BUFFER_COMPUTE_WRITE, 32) \
    /* Texture kinds, SDL_GPUTextureType. */ \
    X(i32, TEXTURE_2D, 0) \
    X(i32, TEXTURE_2D_ARRAY, 1) \
    X(i32, TEXTURE_3D, 2) \
    X(i32, TEXTURE_CUBE, 3) \
    /* Texture usage, SDL_GPUTextureUsageFlags. */ \
    X(i32, SAMPLED, 1) \
    X(i32, COLOR_TARGET, 2) \
    X(i32, DEPTH_TARGET, 4) \
    X(i32, STORAGE_READ, 8) \
    X(i32, COMPUTE_READ, 16) \
    X(i32, COMPUTE_WRITE, 32) \
    X(i32, COMPUTE_READ_WRITE, 64) \
    /* Texture formats. DEPTH is the best supported of DEPTH32F and DEPTH24. */ \
    X(i32, RGBA8, 1) \
    X(i32, BGRA8, 2) \
    X(i32, RGBA8_SRGB, 3) \
    X(i32, R8, 4) \
    X(i32, RG8, 5) \
    X(i32, RGBA16F, 6) \
    X(i32, RGBA32F, 7) \
    X(i32, R16F, 8) \
    X(i32, RG16F, 9) \
    X(i32, R32F, 10) \
    X(i32, RG32F, 11) \
    X(i32, R32UI, 12) \
    X(i32, RGBA8UI, 13) \
    X(i32, RGB10A2, 14) \
    X(i32, RG11B10F, 15) \
    X(i32, R16, 26) \
    X(i32, RG16, 27) \
    X(i32, RGBA16, 28) \
    X(i32, DEPTH16, 16) \
    X(i32, DEPTH24, 17) \
    X(i32, DEPTH32F, 18) \
    X(i32, DEPTH24_STENCIL8, 19) \
    X(i32, DEPTH, 20) \
    X(i32, DEPTH32F_STENCIL8, 36) \
    /* DEPTH_STENCIL is DEPTH32F_STENCIL8 where it can be a target, else \
       DEPTH24_STENCIL8. */ \
    X(i32, DEPTH_STENCIL, 37) \
    /* Block-compressed formats, 4 x 4 texels a block: BC1 (RGB with 1-bit \
       alpha, 8 bytes a block), BC2 (RGB with 4-bit alpha, 16), BC3 (RGBA, \
       16), BC4 (R, 8), BC5 (RG, 16), BC7 (RGBA, 16), the sRGB forms of BC1, \
       BC2, BC3 and BC7, and BC6H (RGB half floats, 16), unsigned and \
       signed. Sampled only: uploaded and read back as blocks. */ \
    X(i32, BC1, 21) \
    X(i32, BC3, 22) \
    X(i32, BC4, 23) \
    X(i32, BC5, 24) \
    X(i32, BC7, 25) \
    X(i32, BC2, 29) \
    X(i32, BC1_SRGB, 30) \
    X(i32, BC2_SRGB, 31) \
    X(i32, BC3_SRGB, 32) \
    X(i32, BC7_SRGB, 33) \
    X(i32, BC6H_UFLOAT, 34) \
    X(i32, BC6H_FLOAT, 35) \
    /* Samplers: SDL_GPUFilter, SDL_GPUSamplerAddressMode, SDL_GPUCompareOp. */ \
    X(i32, NEAREST, 0) \
    X(i32, LINEAR, 1) \
    X(i32, REPEAT, 0) \
    X(i32, MIRROR, 1) \
    X(i32, CLAMP, 2) \
    X(i32, COMPARE_NEVER, 1) \
    X(i32, COMPARE_LESS, 2) \
    X(i32, COMPARE_EQUAL, 3) \
    X(i32, COMPARE_LESS_EQUAL, 4) \
    X(i32, COMPARE_GREATER, 5) \
    X(i32, COMPARE_NOT_EQUAL, 6) \
    X(i32, COMPARE_GREATER_EQUAL, 7) \
    X(i32, COMPARE_ALWAYS, 8) \
    /* Pipelines: SDL_GPUPrimitiveType, SDL_GPUCullMode, then blend modes. */ \
    X(i32, TRIANGLES, 0) \
    X(i32, TRIANGLE_STRIP, 1) \
    X(i32, LINES, 2) \
    X(i32, LINE_STRIP, 3) \
    X(i32, POINTS, 4) \
    X(i32, CULL_NONE, 0) \
    X(i32, CULL_FRONT, 1) \
    X(i32, CULL_BACK, 2) \
    X(i32, BLEND_NONE, 0) \
    X(i32, BLEND_ALPHA, 1) \
    X(i32, BLEND_ADD, 2) \
    X(i32, BLEND_PREMULTIPLIED, 3) \
    X(i32, BLEND_MULTIPLY, 4) \
    X(i32, BLEND_CUSTOM, 5) \
    /* Blend factors and operations, for BLEND_CUSTOM: SDL_GPUBlendFactor \
       and SDL_GPUBlendOp. */ \
    X(i32, FACTOR_ZERO, 1) \
    X(i32, FACTOR_ONE, 2) \
    X(i32, FACTOR_SRC_COLOR, 3) \
    X(i32, FACTOR_ONE_MINUS_SRC_COLOR, 4) \
    X(i32, FACTOR_DST_COLOR, 5) \
    X(i32, FACTOR_ONE_MINUS_DST_COLOR, 6) \
    X(i32, FACTOR_SRC_ALPHA, 7) \
    X(i32, FACTOR_ONE_MINUS_SRC_ALPHA, 8) \
    X(i32, FACTOR_DST_ALPHA, 9) \
    X(i32, FACTOR_ONE_MINUS_DST_ALPHA, 10) \
    X(i32, FACTOR_CONSTANT, 11) \
    X(i32, FACTOR_ONE_MINUS_CONSTANT, 12) \
    X(i32, FACTOR_SRC_ALPHA_SATURATE, 13) \
    X(i32, OP_ADD, 1) \
    X(i32, OP_SUBTRACT, 2) \
    X(i32, OP_REVERSE_SUBTRACT, 3) \
    X(i32, OP_MIN, 4) \
    X(i32, OP_MAX, 5) \
    /* Channels a draw writes: SDL_GPUColorComponentFlags. */ \
    X(i32, WRITE_R, 1) \
    X(i32, WRITE_G, 2) \
    X(i32, WRITE_B, 4) \
    X(i32, WRITE_A, 8) \
    X(i32, WRITE_ALL, 15) \
    /* Stencil operations: SDL_GPUStencilOp. */ \
    X(i32, STENCIL_KEEP, 1) \
    X(i32, STENCIL_ZERO, 2) \
    X(i32, STENCIL_REPLACE, 3) \
    X(i32, STENCIL_INCREMENT, 4) \
    X(i32, STENCIL_DECREMENT, 5) \
    X(i32, STENCIL_INVERT, 6) \
    X(i32, STENCIL_INCREMENT_WRAP, 7) \
    X(i32, STENCIL_DECREMENT_WRAP, 8) \
    /* Vertex attribute formats, for PipelineDesc.formats. */ \
    X(u8, VERTEX_FLOAT, 1) \
    X(u8, VERTEX_FLOAT2, 2) \
    X(u8, VERTEX_FLOAT3, 3) \
    X(u8, VERTEX_FLOAT4, 4) \
    X(u8, VERTEX_INT, 5) \
    X(u8, VERTEX_INT2, 6) \
    X(u8, VERTEX_INT3, 7) \
    X(u8, VERTEX_INT4, 8) \
    X(u8, VERTEX_UINT, 9) \
    X(u8, VERTEX_UINT2, 10) \
    X(u8, VERTEX_UINT3, 11) \
    X(u8, VERTEX_UINT4, 12) \
    X(u8, VERTEX_UBYTE4_NORM, 13) \
    X(u8, VERTEX_BYTE4_NORM, 14) \
    X(u8, VERTEX_UBYTE4, 15) \
    X(u8, VERTEX_SHORT2, 16) \
    X(u8, VERTEX_SHORT4, 17) \
    X(u8, VERTEX_USHORT2_NORM, 18) \
    X(u8, VERTEX_USHORT4_NORM, 19) \
    X(u8, VERTEX_HALF2, 20) \
    X(u8, VERTEX_HALF4, 21) \
    /* load_texture() flags. */ \
    X(i32, LOAD_MIPS, 1) \
    X(i32, LOAD_SRGB, 2) \
    /* Mouse buttons. */ \
    X(i32, MOUSE_LEFT, 1) \
    X(i32, MOUSE_MIDDLE, 2) \
    X(i32, MOUSE_RIGHT, 3) \
    X(i32, MOUSE_X1, 4) \
    X(i32, MOUSE_X2, 5) \
    /* Input events, and the modifier keys held with a key. */ \
    X(i32, EVENT_KEY, 1) \
    X(i32, EVENT_TEXT, 2) \
    X(i32, EVENT_MOUSE_MOTION, 3) \
    X(i32, EVENT_MOUSE_BUTTON, 4) \
    X(i32, EVENT_MOUSE_WHEEL, 5) \
    X(i32, EVENT_FOCUS_GAINED, 6) \
    X(i32, EVENT_FOCUS_LOST, 7) \
    X(i32, MOD_SHIFT, 1) \
    X(i32, MOD_CTRL, 2) \
    X(i32, MOD_ALT, 4) \
    X(i32, MOD_GUI, 8)

#define GS_GFX_ENUM(type, name, value) GS_GFX_##name = value,
enum { GS_GFX_CONSTANTS(GS_GFX_ENUM) };
#undef GS_GFX_ENUM

#ifdef __cplusplus
}
#endif

#endif
