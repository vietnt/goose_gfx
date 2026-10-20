/* The gfx layer: buffers, textures and samplers, and moving data between
   them and the program. */

#include "gfx_internal.h"

#include <stdlib.h>

/* --- formats --------------------------------------------------------------- */

bool gfx_texture_format(int32_t format, SDL_GPUTextureFormat *out) {
    switch (format) {
        case GS_GFX_RGBA8: *out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; return true;
        case GS_GFX_BGRA8: *out = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM; return true;
        case GS_GFX_RGBA8_SRGB: *out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB; return true;
        case GS_GFX_R8: *out = SDL_GPU_TEXTUREFORMAT_R8_UNORM; return true;
        case GS_GFX_RG8: *out = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM; return true;
        case GS_GFX_RGBA16F: *out = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; return true;
        case GS_GFX_RGBA32F: *out = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT; return true;
        case GS_GFX_R16F: *out = SDL_GPU_TEXTUREFORMAT_R16_FLOAT; return true;
        case GS_GFX_RG16F: *out = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT; return true;
        case GS_GFX_R32F: *out = SDL_GPU_TEXTUREFORMAT_R32_FLOAT; return true;
        case GS_GFX_RG32F: *out = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT; return true;
        case GS_GFX_R32UI: *out = SDL_GPU_TEXTUREFORMAT_R32_UINT; return true;
        case GS_GFX_RGBA8UI: *out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT; return true;
        case GS_GFX_RGB10A2: *out = SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM; return true;
        case GS_GFX_RG11B10F: *out = SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT; return true;
        case GS_GFX_R16: *out = SDL_GPU_TEXTUREFORMAT_R16_UNORM; return true;
        case GS_GFX_RG16: *out = SDL_GPU_TEXTUREFORMAT_R16G16_UNORM; return true;
        case GS_GFX_RGBA16: *out = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM; return true;
        case GS_GFX_DEPTH16: *out = SDL_GPU_TEXTUREFORMAT_D16_UNORM; return true;
        case GS_GFX_DEPTH24: *out = SDL_GPU_TEXTUREFORMAT_D24_UNORM; return true;
        case GS_GFX_DEPTH32F: *out = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; return true;
        case GS_GFX_DEPTH24_STENCIL8: *out = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT; return true;
        case GS_GFX_DEPTH32F_STENCIL8: *out = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT; return true;
        case GS_GFX_DEPTH_STENCIL:
            /* Apple GPUs have no D24S8, and some others no D32FS8. */
            *out = gfx.dev && SDL_GPUTextureSupportsFormat(
                                  gfx.dev, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                  SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                       ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT
                       : SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
            return true;
        case GS_GFX_BC1: *out = SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM; return true;
        case GS_GFX_BC3: *out = SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM; return true;
        case GS_GFX_BC4: *out = SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM; return true;
        case GS_GFX_BC5: *out = SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM; return true;
        case GS_GFX_BC7: *out = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM; return true;
        case GS_GFX_BC2: *out = SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM; return true;
        case GS_GFX_BC1_SRGB: *out = SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB; return true;
        case GS_GFX_BC2_SRGB: *out = SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB; return true;
        case GS_GFX_BC3_SRGB: *out = SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB; return true;
        case GS_GFX_BC7_SRGB: *out = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB; return true;
        case GS_GFX_BC6H_UFLOAT: *out = SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT; return true;
        case GS_GFX_BC6H_FLOAT: *out = SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT; return true;
        case GS_GFX_DEPTH:
            /* 32-bit float where it can be sampled too, as for a shadow map. */
            *out = gfx.dev && SDL_GPUTextureSupportsFormat(
                                  gfx.dev, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                  SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER)
                       ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                       : SDL_GPU_TEXTUREFORMAT_D24_UNORM;
            return true;
        default:
            return false;
    }
}

static bool gfx_is_depth(SDL_GPUTextureFormat f) {
    return f == SDL_GPU_TEXTUREFORMAT_D16_UNORM || f == SDL_GPU_TEXTUREFORMAT_D24_UNORM ||
           f == SDL_GPU_TEXTUREFORMAT_D32_FLOAT || f == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
           f == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

/* The width and height of a block of `f`: 4 for the block-compressed
   formats, 1 for the rest. */
static uint32_t gfx_block(SDL_GPUTextureFormat f) {
    switch (f) {
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT:
        case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT:
            return 4;
        default:
            return 1;
    }
}

const char *gfx_stage_name(int64_t stage) {
    return stage == GS_GFX_VERTEX ? "vertex" : stage == GS_GFX_FRAGMENT ? "fragment" : "compute";
}

gfx_texture_slot *gfx_texture_slot_of(gs_gfx_texture t) {
    return (gfx_texture_slot *)gfx_table_get(&gfx.textures, t.id);
}

gfx_buffer_slot *gfx_buffer_slot_of(gs_gfx_buffer b) {
    return (gfx_buffer_slot *)gfx_table_get(&gfx.buffers, b.id);
}

/* --- transfers ------------------------------------------------------------- */

/* A mapped upload or download buffer of `size` bytes. */
static SDL_GPUTransferBuffer *gfx_transfer(uint32_t size, bool upload) {
    SDL_GPUTransferBufferCreateInfo ci;
    SDL_zero(ci);
    ci.usage = upload ? SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ci.size = size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gfx.dev, &ci);
    if (!tb) gfx_sdl_fail("creating a transfer buffer");
    return tb;
}

/* --- the upload ring ---------------------------------------------------------
   Every upload is staged in one of two transfer buffers, upload_size bytes
   each, appended to the one being filled. When an upload does not fit what
   is left of it, its copies go to the GPU (the frame's command buffer is
   submitted early if they are in it) and the other buffer is filled from
   its start, once every submit that read from it has finished: the CPU
   waits for the GPU there, and never holds more staging than the two
   buffers. A region bigger than a buffer goes in parts, by rows (block rows
   of a compressed format) and 3D slices, and a buffer's bytes in parts of
   whole words.

   At the end of a frame that staged something, the other buffer is taken
   from its start if the GPU is done with it, so uploads made every frame
   reuse the start of each buffer rather than walking through both. After
   GFX_UPLOAD_IDLE_FRAMES frames in a row each staging less than 1/64 of
   the capacity, both buffers are released (SDL frees them once the GPU is
   done with them) and those made after are GFX_UPLOAD_SMALL; an upload
   that does not fit what is left of a small one lets both go and goes on
   at the full capacity. So a program that has loaded its world keeps 16 MB
   of staging, however long it runs, and one that uploads nothing keeps
   none.

   Where a copy is recorded does not change: a new resource's first
   contents go into fresh_cmd, which can be recorded anywhere, even inside a
   pass, since nothing earlier can be using the resource, and which is
   submitted ahead of the frame's command buffer every time that is; any
   other copy goes into the command buffer being recorded, in program order
   with the draws around it, and so cannot happen inside a pass. Copies in
   the frame's command buffer keep their place among the draws, and those
   in fresh_cmd land before any draw that can use their resource.

   Texture rows are staged 256 bytes apart and copies start 512 bytes
   apart, which Direct3D 12 needs to copy from the buffer itself rather than
   from a temporary copy of the rows it makes otherwise. */

/* 128 MB a buffer on a GPU with 4 GB or more, 64 MB otherwise or when SDL
   cannot tell. */
uint32_t gfx_upload_size_for_device(void) {
    SDL_GPUMemoryInfo info;
    SDL_zero(info);
    bool known = SDL_GetGPUMemoryInfo(gfx.dev, &info) && info.kind != 0;
    return known && info.bytes >= (Uint64)4 << 30 ? 128u << 20 : 64u << 20;
}

static SDL_GPUCommandBuffer *gfx_fresh_cmd(void) {
    if (!gfx.fresh_cmd) {
        gfx.fresh_cmd = SDL_AcquireGPUCommandBuffer(gfx.dev);
        if (!gfx.fresh_cmd) gfx_sdl_fail("acquiring a command buffer");
    }
    return gfx.fresh_cmd;
}

/* Gives `f` to the buffer, letting go of those of its fences that have
   signalled. */
static void gfx_upload_add_fence(gfx_upload_slot *u, gfx_fence *f) {
    int n = 0;
    for (int i = 0; i < u->nfences; i++) {
        if (gfx_fence_done(u->fences[i])) gfx_fence_drop(u->fences[i]);
        else u->fences[n++] = u->fences[i];
    }
    u->nfences = n;
    if (u->nfences == u->fences_cap) {
        int cap = u->fences_cap ? u->fences_cap * 2 : 4;
        gfx_fence **grown = (gfx_fence **)realloc(u->fences, sizeof *grown * (size_t)cap);
        if (!grown) {
            gfx_fence_wait(f);
            return;
        }
        u->fences = grown;
        u->fences_cap = cap;
    }
    f->refs++;
    u->fences[u->nfences++] = f;
}

void gfx_submit_fresh_uploads(void) {
    SDL_GPUCommandBuffer *cb = gfx.fresh_cmd;
    if (!cb) return;
    gfx.fresh_cmd = NULL;
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    gfx_fence *f = fence ? (gfx_fence *)calloc(1, sizeof *f) : NULL;
    if (!fence) gfx_sdl_fail("submitting uploads");
    else if (!f) SDL_WaitForGPUFences(gfx.dev, true, &fence, 1);
    if (f) {
        f->fence = fence;
        f->refs = 1;
    }
    for (int i = 0; i < GFX_UPLOAD_BUFFERS; i++) {
        gfx_upload_slot *u = &gfx.uploads[i];
        if (!u->in_fresh) continue;
        u->in_fresh = false;
        if (f) gfx_upload_add_fence(u, f);
    }
    if (f) gfx_fence_drop(f);
    else if (fence) SDL_ReleaseGPUFence(gfx.dev, fence);
}

void gfx_uploads_submitted(gfx_fence *f) {
    for (int i = 0; i < GFX_UPLOAD_BUFFERS; i++) {
        gfx_upload_slot *u = &gfx.uploads[i];
        if (!u->in_cmd) continue;
        u->in_cmd = false;
        if (f) gfx_upload_add_fence(u, f);
    }
}

bool gfx_uploads_in_cmd(void) {
    for (int i = 0; i < GFX_UPLOAD_BUFFERS; i++)
        if (gfx.uploads[i].in_cmd) return true;
    return false;
}

void gfx_release_uploads(void) {
    for (int i = 0; i < GFX_UPLOAD_BUFFERS; i++) {
        gfx_upload_slot *u = &gfx.uploads[i];
        for (int k = 0; k < u->nfences; k++) gfx_fence_drop(u->fences[k]);
        free(u->fences);
        if (u->tb) SDL_ReleaseGPUTransferBuffer(gfx.dev, u->tb);
    }
    memset(gfx.uploads, 0, sizeof gfx.uploads);
    gfx.upload_cur = 0;
}

bool gfx_upload_busy(const gfx_upload_slot *u) {
    if (u->in_cmd || u->in_fresh) return true;
    for (int i = 0; i < u->nfences; i++)
        if (!gfx_fence_done(u->fences[i])) return true;
    return false;
}

/* After a frame's submit: shrinks or releases the buffers after
   GFX_UPLOAD_IDLE_FRAMES light or empty frames, else takes the other one
   from its start if it is free. */
void gfx_uploads_frame_ended(void) {
    uint64_t staged = gfx.upload_frame_bytes;
    gfx.upload_frame_bytes = 0;
    gfx.upload_idle = staged ? 0 : gfx.upload_idle + 1;
    gfx.upload_light = staged < gfx.upload_capacity / 64 ? gfx.upload_light + 1 : 0;
    bool shrink = gfx.upload_size > GFX_UPLOAD_SMALL && gfx.upload_light >= GFX_UPLOAD_IDLE_FRAMES;
    if (shrink || gfx.upload_idle >= GFX_UPLOAD_IDLE_FRAMES) {
        gfx_release_uploads();
        if (gfx.upload_size > GFX_UPLOAD_SMALL) gfx.upload_size = GFX_UPLOAD_SMALL;
        gfx.upload_light = gfx.upload_idle = 0;
        return;
    }
    gfx_upload_slot *next = &gfx.uploads[1 - gfx.upload_cur];
    if (!staged || gfx_upload_busy(next)) return;
    for (int i = 0; i < next->nfences; i++) gfx_fence_drop(next->fences[i]);
    next->nfences = 0;
    next->used = 0;
    gfx.upload_cur = 1 - gfx.upload_cur;
}

/* Sends the buffer's recorded copies to the GPU, so that waiting for it
   waits for something submitted; false when they are in the frame's
   command buffer while a pass is being recorded into it. */
static bool gfx_upload_submit(gfx_upload_slot *u) {
    if (u->in_fresh) gfx_submit_fresh_uploads();
    if (!u->in_cmd) return true;
    if (gfx.pass || gfx.cpass) return false;
    gfx_fence_drop(gfx_submit_fenced());
    return true;
}

/* Where an upload's bytes are staged: in a ring buffer (u), or in a
   transfer buffer of their own (u NULL) when a part of it is bigger than a
   ring buffer, or when a fresh upload inside a pass finds the ring held by
   copies the frame has not submitted yet. */
typedef struct {
    gfx_upload_slot *u;
    SDL_GPUTransferBuffer *tb;
    uint32_t at;
    uint8_t *p;
} gfx_staged;

/* Stages up to `units` units of `unit` bytes, `align`ed, and returns how
   many: at least one, or 0 on failure. */
static uint32_t gfx_stage(uint32_t unit, uint32_t units, uint32_t align, gfx_staged *st) {
    SDL_zero(*st);
    gfx_upload_slot *u = &gfx.uploads[gfx.upload_cur];
    uint32_t at = (u->used + align - 1) / align * align;
    if (gfx.upload_size < gfx.upload_capacity && (uint64_t)at + (uint64_t)unit * units > gfx.upload_size) {
        /* Copies recorded from the small buffers keep them until the GPU
           has made them. */
        gfx_release_uploads();
        gfx.upload_size = gfx.upload_capacity;
        u = &gfx.uploads[0];
        at = 0;
    }
    bool fits = unit <= gfx.upload_size;
    if (fits && u->tb && at + unit > gfx.upload_size) {
        gfx_upload_slot *next = &gfx.uploads[1 - gfx.upload_cur];
        gfx_upload_submit(u);
        if (gfx_upload_submit(next)) {
            for (int i = 0; i < next->nfences; i++) {
                gfx_fence_wait(next->fences[i]);
                gfx_fence_drop(next->fences[i]);
            }
            next->nfences = 0;
            next->used = 0;
            gfx.upload_cur = 1 - gfx.upload_cur;
            u = next;
            at = 0;
        } else {
            fits = false;
        }
    }
    if (fits && !u->tb) {
        u->tb = gfx_transfer(gfx.upload_size, true);
        if (!u->tb) return 0;
        u->used = 0;
        at = 0;
    }
    uint32_t n = units;
    if (fits) {
        uint32_t room = (gfx.upload_size - at) / unit;
        if (n > room) n = room;
        st->u = u;
        st->tb = u->tb;
        st->at = at;
        u->used = at + n * unit;
        int64_t held = 0;
        for (int i = 0; i < GFX_UPLOAD_BUFFERS; i++) held += gfx.uploads[i].used;
        if (held > gfx.upload_peak) gfx.upload_peak = held;
    } else {
        st->tb = gfx_transfer(n * unit, true);
        if (!st->tb) return 0;
    }
    gfx.upload_frame_bytes += (uint64_t)n * unit;
    st->p = (uint8_t *)SDL_MapGPUTransferBuffer(gfx.dev, st->tb, false);
    if (!st->p) {
        gfx_sdl_fail("mapping a transfer buffer");
        if (!st->u) SDL_ReleaseGPUTransferBuffer(gfx.dev, st->tb);
        return 0;
    }
    st->p += st->at;
    return n;
}

/* After the staged bytes are written and their copy recorded: the ring
   buffer now has copies in the command buffer, or the buffer of their own
   goes once the GPU is done with it. */
static void gfx_staged_recorded(gfx_staged *st, bool fresh) {
    if (!st->u) {
        SDL_ReleaseGPUTransferBuffer(gfx.dev, st->tb);
        return;
    }
    if (fresh) st->u->in_fresh = true;
    else st->u->in_cmd = true;
}

/* The command buffer a copy is recorded into, after it is staged: staging
   can submit the frame's command buffer. */
static SDL_GPUCommandBuffer *gfx_upload_cmd(bool fresh) { return fresh ? gfx_fresh_cmd() : gfx_cmd(); }

static SDL_GPUCommandBuffer *gfx_copy_cmd(bool fresh, const char *what) {
    if (fresh) return gfx_fresh_cmd();
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("%s inside a %s pass: copies happen between passes", what,
                   gfx.pass ? "render" : "compute");
        return NULL;
    }
    return gfx_cmd();
}

static bool gfx_copy_allowed(bool fresh, const char *what) {
    if (fresh || !(gfx.pass || gfx.cpass)) return true;
    return gfx_misuse("%s inside a %s pass: copies happen between passes", what,
                      gfx.pass ? "render" : "compute");
}

/* Writes `len` bytes of `data` and then `zeros` zero bytes into `buf` at
   `offset`. `cycle` lets SDL give the buffer fresh memory when a draw still
   pending uses the old, for a rewrite of the whole buffer only. */
static bool gfx_upload_buffer(SDL_GPUBuffer *buf, uint32_t offset, const void *data,
                              uint32_t len, uint32_t zeros, bool fresh, bool cycle,
                              const char *what) {
    uint32_t total = len + zeros;
    if (!total) return true;
    if (!gfx_copy_allowed(fresh, what)) return false;
    for (uint32_t pos = 0; pos < total;) {
        gfx_staged st;
        uint32_t words = gfx_stage(4, (total - pos + 3) / 4, 16, &st);
        if (!words) return false;
        uint32_t n = words * 4 < total - pos ? words * 4 : total - pos;
        uint32_t from_data = pos < len ? (len - pos < n ? len - pos : n) : 0;
        if (from_data) memcpy(st.p, (const uint8_t *)data + pos, from_data);
        if (n > from_data) memset(st.p + from_data, 0, n - from_data);
        SDL_UnmapGPUTransferBuffer(gfx.dev, st.tb);
        SDL_GPUCommandBuffer *cb = gfx_upload_cmd(fresh);
        if (!cb) {
            if (!st.u) SDL_ReleaseGPUTransferBuffer(gfx.dev, st.tb);
            return false;
        }
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
        SDL_GPUTransferBufferLocation src = { st.tb, st.at };
        SDL_GPUBufferRegion dst = { buf, offset + pos, n };
        /* Only the first part may cycle: a later one writes into what it
           cycled to. */
        SDL_UploadToGPUBuffer(cp, &src, &dst, cycle && pos == 0);
        SDL_EndGPUCopyPass(cp);
        gfx_staged_recorded(&st, fresh);
        pos += n;
    }
    return true;
}

/* --- buffers --------------------------------------------------------------- */

gs_gfx_buffer gs_gfx_create_buffer(int64_t usage, int64_t size, gs_gfx_bytes data) {
    gs_gfx_buffer h = { 0 };
    if (!gfx_need_device("buffer")) return h;
    if (size < data.len) size = data.len;
    if (size <= 0 || size > 0x7fffffff) {
        gfx_misuse("a buffer of %lld bytes", (long long)size);
        return h;
    }
    if (!usage || (usage & ~(int64_t)63)) {
        gfx_misuse("buffer usage %lld is not a combination of the BUFFER_ flags", (long long)usage);
        return h;
    }
    SDL_GPUBufferCreateInfo ci;
    SDL_zero(ci);
    ci.usage = (SDL_GPUBufferUsageFlags)usage;
    ci.size = (Uint32)size;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(gfx.dev, &ci);
    if (!buf) {
        gfx_sdl_fail("creating a buffer");
        return h;
    }
    /* Zeroed past the data, rather than left as whatever the memory held. */
    if (!gfx_upload_buffer(buf, 0, data.data, (uint32_t)data.len, (uint32_t)(size - data.len),
                           true, false, "buffer")) {
        SDL_ReleaseGPUBuffer(gfx.dev, buf);
        return h;
    }
    gfx_buffer_slot *s = (gfx_buffer_slot *)gfx_table_add(&gfx.buffers, &h.id);
    if (!s) {
        SDL_ReleaseGPUBuffer(gfx.dev, buf);
        return h;
    }
    s->buf = buf;
    s->size = (uint32_t)size;
    s->usage = (uint32_t)usage;
    return h;
}

uint8_t gs_gfx_update_buffer(gs_gfx_buffer b, int64_t offset, gs_gfx_bytes data) {
    if (!gfx_need_device("update_buffer")) return 0;
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return 0;
    if (offset < 0 || data.len < 0 || offset + data.len > (int64_t)s->size)
        return gfx_misuse("update_buffer: %lld bytes at %lld do not fit a buffer of %u",
                          (long long)data.len, (long long)offset, s->size);
    bool whole = offset == 0 && data.len == (int64_t)s->size;
    return gfx_upload_buffer(s->buf, (uint32_t)offset, data.data, (uint32_t)data.len, 0, false,
                             whole, "update_buffer");
}

/* Metal on macOS wants a buffer copy's offsets and size multiples of 4.
   Direct3D 12's resource states cannot make one buffer a copy's source and
   its destination at once, so a copy within one buffer goes through a
   temporary buffer, on every backend alike. */
uint8_t gs_gfx_copy_buffer(gs_gfx_buffer src, int64_t from, gs_gfx_buffer dst, int64_t to,
                           int64_t size) {
    const char *what = "copy_buffer";
    if (!gfx_need_device(what)) return 0;
    gfx_buffer_slot *s = gfx_buffer_slot_of(src), *t = gfx_buffer_slot_of(dst);
    if (!s || !t) return 0;
    if (from < 0 || to < 0 || size < 0 || from % 4 || to % 4 || size % 4)
        return gfx_misuse("%s: offsets %lld and %lld and size %lld are not all multiples of 4",
                          what, (long long)from, (long long)to, (long long)size);
    if (from + size > (int64_t)s->size || to + size > (int64_t)t->size)
        return gfx_misuse("%s: %lld bytes from %lld of a %u-byte buffer to %lld of a %u-byte one",
                          what, (long long)size, (long long)from, s->size, (long long)to, t->size);
    if (s == t && from < to + size && to < from + size)
        return gfx_misuse("%s: the ranges at %lld and %lld of one buffer overlap", what,
                          (long long)from, (long long)to);
    if (!size) return 1;
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, what);
    if (!cb) return 0;
    SDL_GPUBuffer *tmp = NULL;
    if (s == t) {
        SDL_GPUBufferCreateInfo ci;
        SDL_zero(ci);
        ci.usage = (SDL_GPUBufferUsageFlags)s->usage;
        ci.size = (Uint32)size;
        tmp = SDL_CreateGPUBuffer(gfx.dev, &ci);
        if (!tmp) return gfx_sdl_fail("creating a buffer for a copy within one buffer");
    }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUBufferLocation a = { s->buf, (Uint32)from }, b = { t->buf, (Uint32)to };
    if (tmp) {
        SDL_GPUBufferLocation m = { tmp, 0 };
        SDL_CopyGPUBufferToBuffer(cp, &a, &m, (Uint32)size, false);
        SDL_CopyGPUBufferToBuffer(cp, &m, &b, (Uint32)size, false);
    } else {
        SDL_CopyGPUBufferToBuffer(cp, &a, &b, (Uint32)size, false);
    }
    SDL_EndGPUCopyPass(cp);
    /* SDL frees it once the GPU is through the copies. */
    if (tmp) SDL_ReleaseGPUBuffer(gfx.dev, tmp);
    return 1;
}

static gs_gfx_readback gfx_request_buffer(gs_gfx_buffer b, int64_t offset, int64_t size,
                                          const char *what);
static bool gfx_take(gs_gfx_readback h, void *out, int64_t len, const char *what);

/* A readback taken at once: the copy is recorded after all the work so
   far, and this waits for the GPU to get through it. */
uint8_t gs_gfx_read_buffer(gs_gfx_buffer b, int64_t offset, gs_gfx_bytes out) {
    if (!gfx_need_device("read_buffer")) return 0;
    if (!out.len) {
        gfx_buffer_slot *s = gfx_buffer_slot_of(b);
        if (s && (offset < 0 || offset > (int64_t)s->size))
            return gfx_misuse("read_buffer: offset %lld is outside a buffer of %u",
                              (long long)offset, s->size);
        return s != NULL;
    }
    gs_gfx_readback h = gfx_request_buffer(b, offset, out.len, "read_buffer");
    return h.id && gfx_take(h, out.data, out.len, "read_buffer");
}

int64_t gs_gfx_buffer_size(gs_gfx_buffer b) {
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    return s ? s->size : 0;
}

void gs_gfx_release_buffer(gs_gfx_buffer b) {
    if (!gfx_need_device("release_buffer")) return;
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return;
    SDL_ReleaseGPUBuffer(gfx.dev, s->buf);
    gfx_table_remove(&gfx.buffers, b.id);
}

/* --- textures -------------------------------------------------------------- */

static int32_t gfx_mip_size(int32_t size, int32_t mip) {
    size >>= mip;
    return size < 1 ? 1 : size;
}

static SDL_GPUSampleCount gfx_samples(int32_t n) {
    switch (n) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

gs_gfx_texture gs_gfx_create_texture(const gs_gfx_texture_desc *desc) {
    gs_gfx_texture h = { 0 };
    if (!gfx_need_device("texture")) return h;
    gs_gfx_texture_desc d = *desc;
    SDL_GPUTextureFormat format;
    if (!gfx_texture_format(d.format, &format)) {
        gfx_misuse("texture format %d is not one of gfx's formats", d.format);
        return h;
    }
    if (d.kind < GS_GFX_TEXTURE_2D || d.kind > GS_GFX_TEXTURE_CUBE) {
        gfx_misuse("texture kind %d is not one of TEXTURE_2D, _2D_ARRAY, _3D or _CUBE", d.kind);
        return h;
    }
    if (d.width < 1 || d.height < 1 || d.width > 16384 || d.height > 16384) {
        gfx_misuse("a texture of %d x %d", d.width, d.height);
        return h;
    }
    if (d.kind == GS_GFX_TEXTURE_CUBE && d.width != d.height) {
        gfx_misuse("a cube texture must be square, not %d x %d", d.width, d.height);
        return h;
    }
    if (d.kind == GS_GFX_TEXTURE_2D || d.kind == GS_GFX_TEXTURE_CUBE) d.depth = 1;
    if (d.depth < 1 || d.depth > 2048) {
        gfx_misuse("a texture %s of %d", d.kind == GS_GFX_TEXTURE_3D ? "depth" : "layer count",
                   d.depth);
        return h;
    }
    if (d.samples != 2 && d.samples != 4 && d.samples != 8) d.samples = 1;
    int32_t largest = d.width > d.height ? d.width : d.height;
    if (d.kind == GS_GFX_TEXTURE_3D && d.depth > largest) largest = d.depth;
    int32_t chain = 1;
    while (largest >> chain) chain++;
    if (d.mips <= 0 || d.mips > chain) d.mips = chain;
    uint32_t usage = (uint32_t)d.usage;
    if (!usage || (usage & ~127u)) {
        gfx_misuse("texture usage %d is not a combination of the usage flags", d.usage);
        return h;
    }
    bool depth = gfx_is_depth(format);
    if (depth != ((usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) != 0)) {
        gfx_misuse(depth ? "a depth format needs DEPTH_TARGET usage"
                         : "DEPTH_TARGET usage needs a depth format");
        return h;
    }
    if (d.samples > 1 && (d.kind != GS_GFX_TEXTURE_2D || d.mips != 1 ||
                          (usage & ~(uint32_t)(SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                               SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)))) {
        gfx_misuse("a multisampled texture is a 2D target with one mip, drawn into and resolved, "
                   "not sampled or stored to");
        return h;
    }
    /* Generating mips draws into each level, which needs a color target. */
    bool compressed = gfx_block(format) > 1;
    if (compressed && usage != SDL_GPU_TEXTUREUSAGE_SAMPLER) {
        gfx_misuse("a compressed texture is SAMPLED only: uploaded as blocks, not drawn or stored into");
        return h;
    }
    /* Direct3D 12 requires it, so every backend does. */
    if (compressed && (d.width % 4 || d.height % 4)) {
        gfx_misuse("a compressed texture of %d x %d: its width and height are multiples of 4",
                   d.width, d.height);
        return h;
    }
    if (d.mips > 1 && (usage & SDL_GPU_TEXTUREUSAGE_SAMPLER) && !depth && !compressed)
        usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    SDL_GPUTextureType type = (SDL_GPUTextureType)d.kind;
    /* Reading a depth texture back samples it, so one is sampled where the
       device can. */
    if (depth && d.samples == 1 &&
        SDL_GPUTextureSupportsFormat(gfx.dev, format, type, usage | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!SDL_GPUTextureSupportsFormat(gfx.dev, format, type, usage)) {
        gfx_fail("this device does not support texture format %d with usage %u", d.format, usage);
        return h;
    }
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = type;
    ci.format = format;
    ci.usage = usage;
    ci.width = (Uint32)d.width;
    ci.height = (Uint32)d.height;
    ci.layer_count_or_depth = (Uint32)(d.kind == GS_GFX_TEXTURE_CUBE ? 6 : d.depth);
    ci.num_levels = (Uint32)d.mips;
    ci.sample_count = gfx_samples(d.samples);
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(gfx.dev, &ci);
    if (!tex) {
        gfx_sdl_fail("creating a texture");
        return h;
    }
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_add(&gfx.textures, &h.id);
    if (!s) {
        SDL_ReleaseGPUTexture(gfx.dev, tex);
        return h;
    }
    s->tex = tex;
    s->format = format;
    s->usage = usage;
    s->desc = d;
    s->desc.usage = (int32_t)usage;
    return h;
}

/* The region of `s` that `r` names, with w == 0 as the whole mip; a misuse
   if it is not all inside the texture. */
static bool gfx_region(gfx_texture_slot *s, const gs_gfx_region *r, SDL_GPUTextureRegion *out,
                       const char *what) {
    const gs_gfx_texture_desc *d = &s->desc;
    if (r->mip < 0 || r->mip >= d->mips)
        return gfx_misuse("%s: mip %d of a texture with %d", what, r->mip, d->mips);
    int32_t layers = d->kind == GS_GFX_TEXTURE_CUBE ? 6
                   : d->kind == GS_GFX_TEXTURE_2D_ARRAY ? d->depth : 1;
    if (r->layer < 0 || r->layer >= layers)
        return gfx_misuse("%s: layer %d of a texture with %d", what, r->layer, layers);
    int32_t mw = gfx_mip_size(d->width, r->mip), mh = gfx_mip_size(d->height, r->mip);
    int32_t md = d->kind == GS_GFX_TEXTURE_3D ? gfx_mip_size(d->depth, r->mip) : 1;
    SDL_zero(*out);
    out->texture = s->tex;
    out->mip_level = (Uint32)r->mip;
    out->layer = (Uint32)r->layer;
    if (r->w == 0) {
        out->w = (Uint32)mw;
        out->h = (Uint32)mh;
        out->d = (Uint32)md;
        return true;
    }
    int32_t h = r->h ? r->h : 1, dd = r->d ? r->d : 1;
    if (r->x < 0 || r->y < 0 || r->z < 0 || r->w < 0 || h < 0 || dd < 0 || r->x + r->w > mw ||
        r->y + h > mh || r->z + dd > md)
        return gfx_misuse("%s: region %d,%d,%d + %d x %d x %d is outside the %d x %d x %d mip",
                          what, r->x, r->y, r->z, r->w, h, dd, mw, mh, md);
    /* A compressed region is whole blocks, but for those the mip's right or
       bottom edge cuts short. */
    uint32_t b = gfx_block(s->format);
    if (b > 1 && (r->x % b || r->y % b || (r->w % b && r->x + r->w != mw) ||
                  (h % b && r->y + h != mh)))
        return gfx_misuse("%s: region %d,%d + %d x %d of a compressed texture is not whole %u x %u "
                          "blocks", what, r->x, r->y, r->w, h, b, b);
    out->x = (Uint32)r->x;
    out->y = (Uint32)r->y;
    out->z = (Uint32)r->z;
    out->w = (Uint32)r->w;
    out->h = (Uint32)h;
    out->d = (Uint32)dd;
    return true;
}

/* The bytes a region of `s` takes in memory, tightly packed: whole blocks
   of a compressed format. */
static int64_t gfx_region_bytes(gfx_texture_slot *s, const SDL_GPUTextureRegion *reg) {
    return (int64_t)SDL_CalculateGPUTextureFormatSize(s->format, reg->w, reg->h, reg->d);
}

static bool gfx_texture_bytes(gfx_texture_slot *s, const SDL_GPUTextureRegion *reg, int64_t len,
                              const char *what) {
    int64_t want = gfx_region_bytes(s, reg);
    if (len != want)
        return gfx_misuse("%s: %lld bytes for a %u x %u x %u region of %lld", what,
                          (long long)len, reg->w, reg->h, reg->d, (long long)want);
    return true;
}

/* The row length and rows of a region as a transfer lays it out: whole
   blocks, which Vulkan requires of a compressed format's strides. */
static void gfx_transfer_layout(SDL_GPUTextureTransferInfo *t, const SDL_GPUTextureRegion *reg,
                                SDL_GPUTextureFormat f) {
    uint32_t b = gfx_block(f);
    t->pixels_per_row = (reg->w + b - 1) / b * b;
    t->rows_per_layer = (reg->h + b - 1) / b * b;
}

/* `data` is the region's rows tightly packed, as update_texture takes
   them; they are staged in parts of whole (block) rows of one slice each. */
static bool gfx_upload_texture(const SDL_GPUTextureRegion *reg, SDL_GPUTextureFormat format,
                               const void *data, bool fresh, const char *what) {
    if (!gfx_copy_allowed(fresh, what)) return false;
    uint32_t b = gfx_block(format), block_bytes = SDL_GPUTextureFormatTexelBlockSize(format);
    uint32_t row = (reg->w + b - 1) / b * block_bytes, rows = (reg->h + b - 1) / b;
    uint32_t pitch = (row + 255) / 256 * 256;
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t z = 0; z < reg->d; z++) {
        for (uint32_t y = 0; y < rows;) {
            gfx_staged st;
            uint32_t n = gfx_stage(pitch, rows - y, 512, &st);
            if (!n) return false;
            const uint8_t *from = src + ((size_t)z * rows + y) * row;
            if (pitch == row) memcpy(st.p, from, (size_t)n * row);
            else
                for (uint32_t i = 0; i < n; i++) memcpy(st.p + (size_t)i * pitch, from + (size_t)i * row, row);
            SDL_UnmapGPUTransferBuffer(gfx.dev, st.tb);
            SDL_GPUCommandBuffer *cb = gfx_upload_cmd(fresh);
            if (!cb) {
                if (!st.u) SDL_ReleaseGPUTransferBuffer(gfx.dev, st.tb);
                return false;
            }
            SDL_GPUTextureRegion part = *reg;
            part.y = reg->y + y * b;
            part.h = n * b < reg->h - y * b ? n * b : reg->h - y * b;
            part.z = reg->z + z;
            part.d = 1;
            SDL_GPUTextureTransferInfo t;
            SDL_zero(t);
            t.transfer_buffer = st.tb;
            t.offset = st.at;
            t.pixels_per_row = pitch / block_bytes * b;
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
            SDL_UploadToGPUTexture(cp, &t, &part, false);
            SDL_EndGPUCopyPass(cp);
            gfx_staged_recorded(&st, fresh);
            y += n;
        }
    }
    return true;
}

uint8_t gs_gfx_update_texture(gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes data) {
    if (!gfx_need_device("update_texture")) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return 0;
    if (s->desc.samples > 1) return gfx_misuse("update_texture: a multisampled texture");
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, region, &reg, "update_texture") ||
        !gfx_texture_bytes(s, &reg, data.len, "update_texture"))
        return 0;
    return gfx_upload_texture(&reg, s->format, data.data, false, "update_texture");
}

uint8_t gs_gfx_copy_buffer_to_texture(gs_gfx_buffer b, int64_t offset, gs_gfx_texture t,
                                      const gs_gfx_region *region) {
    const char *what = "copy_buffer_to_texture";
    if (!gfx_need_device(what)) return 0;
    gfx_buffer_slot *bs = gfx_buffer_slot_of(b);
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!bs || !s) return 0;
    if (s->desc.samples > 1) return gfx_misuse("%s: a multisampled texture", what);
    if (gfx_is_depth(s->format)) return gfx_misuse("%s: a depth texture", what);
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, region, &reg, what)) return 0;
    /* Vulkan wants the offset a multiple of 4 and of a texel's (or block's)
       bytes. */
    int64_t texel = (int64_t)SDL_GPUTextureFormatTexelBlockSize(s->format);
    int64_t bytes = gfx_region_bytes(s, &reg);
    if (offset < 0 || offset % 4 || offset % texel)
        return gfx_misuse("%s: offset %lld is not a multiple of 4 and of the %lld bytes of a texel",
                          what, (long long)offset, (long long)texel);
    if (offset + bytes > (int64_t)bs->size)
        return gfx_misuse("%s: %lld bytes from offset %lld of a %u-byte buffer", what,
                          (long long)bytes, (long long)offset, bs->size);
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, what);
    if (!cb) return 0;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUBufferTextureCopyInfo src;
    SDL_zero(src);
    src.buffer = bs->buf;
    src.offset = (Uint32)offset;
    SDL_CopyGPUBufferToTexture(cp, &src, &reg, false);
    SDL_EndGPUCopyPass(cp);
    return 1;
}

/* SDL copies between textures of one format, and within one texture only
   between mips or layers: its Vulkan backend moves the whole of a mip's
   layer to the source layout and then to the destination's. */
uint8_t gs_gfx_copy_texture(gs_gfx_texture src, const gs_gfx_region *from, gs_gfx_texture dst,
                            const gs_gfx_region *to) {
    const char *what = "copy_texture";
    if (!gfx_need_device(what)) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(src), *t = gfx_texture_slot_of(dst);
    if (!s || !t) return 0;
    if (s->desc.samples > 1 || t->desc.samples > 1)
        return gfx_misuse("%s: a multisampled texture", what);
    if (gfx_is_depth(s->format) || gfx_is_depth(t->format))
        return gfx_misuse("%s: a depth texture", what);
    if (s->format != t->format) return gfx_misuse("%s: textures of different formats", what);
    SDL_GPUTextureRegion a, b;
    if (!gfx_region(s, from, &a, what)) return 0;
    gs_gfx_region at = *to;
    at.w = (int32_t)a.w;
    at.h = (int32_t)a.h;
    at.d = (int32_t)a.d;
    if (!gfx_region(t, &at, &b, what)) return 0;
    if (s == t && a.mip_level == b.mip_level && a.layer == b.layer)
        return gfx_misuse("%s: within one mip of one layer of a texture", what);
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, what);
    if (!cb) return 0;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureLocation sl = { a.texture, a.mip_level, a.layer, a.x, a.y, a.z };
    SDL_GPUTextureLocation dl = { b.texture, b.mip_level, b.layer, b.x, b.y, b.z };
    SDL_CopyGPUTextureToTexture(cp, &sl, &dl, a.w, a.h, a.d, false);
    SDL_EndGPUCopyPass(cp);
    return 1;
}

/* --- readbacks --------------------------------------------------------------
   A download is recorded in the command buffer being recorded, after all the
   work before it, into a transfer buffer of its own, and is pending until
   that command buffer is submitted (by frame(), flush() or a take). Taking it
   waits for that submit's fence; a blocking read is a request taken at once. */

static gfx_readback_slot *gfx_readback_new(uint32_t size, gs_gfx_readback *h) {
    SDL_GPUTransferBuffer *tb = gfx_transfer(size, false);
    if (!tb) return NULL;
    gfx_readback_slot *r = (gfx_readback_slot *)gfx_table_add(&gfx.readbacks, &h->id);
    if (!r) {
        SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
        return NULL;
    }
    r->tb = tb;
    r->size = size;
    gfx.readbacks_pending++;
    return r;
}

static void gfx_readback_free(gfx_readback_slot *r) {
    if (!r->fence && !r->failed) gfx.readbacks_pending--;
    SDL_ReleaseGPUTransferBuffer(gfx.dev, r->tb);
    gfx_fence_drop(r->fence);
}

/* A multisampled texture reads back resolved, which SDL does for color
   formats other than integer ones. */
static bool gfx_readable(gfx_texture_slot *s, const char *what) {
    if (s->desc.samples > 1 && gfx_is_depth(s->format))
        return gfx_misuse("%s: a multisampled depth texture, which cannot be resolved", what);
    if (s->desc.samples > 1 && (s->format == SDL_GPU_TEXTUREFORMAT_R32_UINT ||
                                s->format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT))
        return gfx_misuse("%s: a multisampled integer texture, which cannot be resolved", what);
    return true;
}

/* Resolves multisampled `s` into a texture of its own, in a render pass
   that keeps the samples, for `reg` to download from instead. */
static SDL_GPUTexture *gfx_resolve_for_read(SDL_GPUCommandBuffer *cb, gfx_texture_slot *s,
                                            SDL_GPUTextureRegion *reg) {
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = s->format;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = (Uint32)s->desc.width;
    ci.height = (Uint32)s->desc.height;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *resolved = SDL_CreateGPUTexture(gfx.dev, &ci);
    if (!resolved) {
        gfx_sdl_fail("creating a texture to resolve into");
        return NULL;
    }
    SDL_GPUColorTargetInfo ct;
    SDL_zero(ct);
    ct.texture = s->tex;
    ct.load_op = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
    ct.resolve_texture = resolved;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cb, &ct, 1, NULL));
    reg->texture = resolved;
    return resolved;
}

/* gfx.goose reads a depth texture by drawing it into an R32F target, which
   SDL_GPU can download on every backend; what reaches here is a depth read
   that cannot go that way, given a valid region. */
static bool gfx_depth_unreadable(gfx_texture_slot *s, const SDL_GPUTextureRegion *reg,
                                 int64_t len, const char *what) {
    int64_t want = 4 * (int64_t)reg->w * reg->h;
    if (len != want)
        return gfx_misuse("%s: %lld bytes for a %u x %u region of a depth texture, read as f32: %lld",
                          what, (long long)len, reg->w, reg->h, (long long)want);
    if (!(s->usage & SDL_GPU_TEXTUREUSAGE_SAMPLER))
        return gfx_fail("%s: this device cannot sample depth format %d, so it cannot read it back",
                        what, s->desc.format);
    return gfx_misuse("%s: a depth texture is read back from a 2D texture, an array layer or a "
                      "cube face, not multisampled", what);
}

/* Records the download of `region` of `t`; `len`, when not -1, is the
   number of bytes the caller wants, a misuse if it is not the region's. */
static gs_gfx_readback gfx_request_texture(gs_gfx_texture t, const gs_gfx_region *region,
                                           int64_t len, const char *what) {
    gs_gfx_readback h = { 0 };
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s || !gfx_readable(s, what)) return h;
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, region, &reg, what)) return h;
    int64_t size = gfx_region_bytes(s, &reg);
    if (gfx_is_depth(s->format)) {
        gfx_depth_unreadable(s, &reg, len < 0 ? 4 * (int64_t)reg.w * reg.h : len, what);
        return h;
    }
    if (len >= 0 && !gfx_texture_bytes(s, &reg, len, what)) return h;
    /* SDL 3.4's Direct3D 12 download of a compressed texture copies the
       region's texel rows rather than its block rows out of its staging
       buffer, past the end of a tightly sized one: refused until SDL is
       fixed. */
    if (gfx_block(s->format) > 1 && SDL_strcmp(SDL_GetGPUDeviceDriver(gfx.dev), "direct3d12") == 0) {
        gfx_fail("%s: compressed textures cannot be read back on Direct3D 12 with SDL 3.4 (its "
                 "download overruns); upload and sampling work", what);
        return h;
    }
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, what);
    if (!cb) return h;
    SDL_GPUTexture *resolved = NULL;
    if (s->desc.samples > 1 && !(resolved = gfx_resolve_for_read(cb, s, &reg))) return h;
    gfx_readback_slot *r = gfx_readback_new((uint32_t)size, &h);
    if (r) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
        SDL_GPUTextureTransferInfo dst;
        SDL_zero(dst);
        dst.transfer_buffer = r->tb;
        gfx_transfer_layout(&dst, &reg, s->format);
        SDL_DownloadFromGPUTexture(cp, &reg, &dst);
        SDL_EndGPUCopyPass(cp);
    }
    /* SDL frees it once the command buffer is done with it. */
    if (resolved) SDL_ReleaseGPUTexture(gfx.dev, resolved);
    return h;
}

static gs_gfx_readback gfx_request_buffer(gs_gfx_buffer b, int64_t offset, int64_t size,
                                          const char *what) {
    gs_gfx_readback h = { 0 };
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return h;
    if (offset < 0 || size <= 0 || offset + size > (int64_t)s->size) {
        gfx_misuse("%s: %lld bytes at %lld are not all in a buffer of %u", what, (long long)size,
                   (long long)offset, s->size);
        return h;
    }
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, what);
    if (!cb) return h;
    gfx_readback_slot *r = gfx_readback_new((uint32_t)size, &h);
    if (!r) return h;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUBufferRegion src = { s->buf, (Uint32)offset, (Uint32)size };
    SDL_GPUTransferBufferLocation dst = { r->tb, 0 };
    SDL_DownloadFromGPUBuffer(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    return h;
}

/* Waits for readback `h`, submitting it first if it is pending, copies it
   to `out` and frees it. */
static bool gfx_take(gs_gfx_readback h, void *out, int64_t len, const char *what) {
    gfx_readback_slot *r = (gfx_readback_slot *)gfx_table_get(&gfx.readbacks, h.id);
    if (!r) return false;
    if (len != (int64_t)r->size)
        return gfx_misuse("%s: %lld bytes for a readback of %u", what, (long long)len, r->size);
    if (!r->fence && !r->failed) {
        if (gfx.pass || gfx.cpass)
            return gfx_misuse("%s of a readback not yet submitted, inside a %s pass: end the pass "
                              "first", what, gfx.pass ? "render" : "compute");
        gfx_fence_drop(gfx_submit_fenced());
    }
    bool ok = !r->failed && gfx_fence_wait(r->fence);
    if (r->failed) gfx_fail("%s: the commands that read it back could not be submitted", what);
    if (ok) {
        void *p = SDL_MapGPUTransferBuffer(gfx.dev, r->tb, false);
        memcpy(out, p, r->size);
        SDL_UnmapGPUTransferBuffer(gfx.dev, r->tb);
    }
    gfx_readback_free(r);
    gfx_table_remove(&gfx.readbacks, h.id);
    return ok;
}

uint8_t gs_gfx_read_texture(gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes out) {
    if (!gfx_need_device("read_texture")) return 0;
    gs_gfx_readback h = gfx_request_texture(t, region, out.len, "read_texture");
    return h.id && gfx_take(h, out.data, out.len, "read_texture");
}

gs_gfx_readback gs_gfx_read_texture_async(gs_gfx_texture t, const gs_gfx_region *region) {
    gs_gfx_readback h = { 0 };
    if (!gfx_need_device("read_texture_async")) return h;
    return gfx_request_texture(t, region, -1, "read_texture_async");
}

gs_gfx_readback gs_gfx_read_buffer_async(gs_gfx_buffer b, int64_t offset, int64_t size) {
    gs_gfx_readback h = { 0 };
    if (!gfx_need_device("read_buffer_async")) return h;
    return gfx_request_buffer(b, offset, size, "read_buffer_async");
}

uint8_t gs_gfx_readback_ready(gs_gfx_readback h) {
    if (!gfx_need_device("ready")) return 0;
    gfx_readback_slot *r = (gfx_readback_slot *)gfx_table_get(&gfx.readbacks, h.id);
    if (!r) return 0;
    return r->failed || (r->fence && gfx_fence_done(r->fence));
}

int64_t gs_gfx_readback_size(gs_gfx_readback h) {
    gfx_readback_slot *r = (gfx_readback_slot *)gfx_table_find(&gfx.readbacks, h.id);
    return r ? r->size : 0;
}

uint8_t gs_gfx_readback_take(gs_gfx_readback h, gs_gfx_bytes out) {
    if (!gfx_need_device("take")) return 0;
    return gfx_take(h, out.data, out.len, "take");
}

void gs_gfx_release_readback(gs_gfx_readback h) {
    if (!gfx_need_device("release_readback")) return;
    gfx_readback_slot *r = (gfx_readback_slot *)gfx_table_get(&gfx.readbacks, h.id);
    if (!r) return;
    gfx_readback_free(r);
    gfx_table_remove(&gfx.readbacks, h.id);
}

int64_t gs_gfx_region_size(gs_gfx_texture t, const gs_gfx_region *region) {
    if (!gfx_need_device("region_size")) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return 0;
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, region, &reg, "region_size")) return 0;
    return gfx_is_depth(s->format) ? 4 * (int64_t)reg.w * reg.h : gfx_region_bytes(s, &reg);
}

uint8_t gs_gfx_save_png(gs_gfx_texture t, gs_gfx_bytes path) {
    if (!gfx_need_device("save_png")) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s || !gfx_readable(s, "save_png")) return 0;
    if (gfx_is_depth(s->format)) return gfx_misuse("save_png: a depth texture");
    SDL_PixelFormat pf;
    if (s->format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
        s->format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)
        pf = SDL_PIXELFORMAT_RGBA32;
    else if (s->format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)
        pf = SDL_PIXELFORMAT_BGRA32;
    else
        return gfx_misuse("save_png: only an RGBA8, RGBA8_SRGB or BGRA8 texture");
    gs_gfx_region r = { 0 };
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, &r, &reg, "save_png")) return 0;
    uint32_t len = reg.w * reg.h * 4;
    uint8_t *pixels = (uint8_t *)malloc(len);
    gs_gfx_readback rb = gfx_request_texture(t, &r, len, "save_png");
    bool ok = rb.id && gfx_take(rb, pixels, len, "save_png");
    /* The screen is saved as it is shown: opaque, whatever alpha drawing
       left in it. */
    if (ok && t.id == gfx.screen.id)
        for (uint32_t i = 3; i < len; i += 4) pixels[i] = 255;
    if (ok) {
        char buf[1024];
        size_t n = path.len < 0 ? 0 : (size_t)path.len;
        if (n >= sizeof buf) n = sizeof buf - 1;
        if (n) memcpy(buf, path.data, n);
        buf[n] = 0;
        SDL_Surface *surf = SDL_CreateSurfaceFrom((int)reg.w, (int)reg.h, pf, pixels,
                                                  (int)reg.w * 4);
        ok = surf && SDL_SavePNG(surf, buf);
        if (!ok) gfx_sdl_fail("save_png");
        SDL_DestroySurface(surf);
    }
    free(pixels);
    return ok;
}

gs_gfx_texture gs_gfx_load_texture(gs_gfx_bytes path, int64_t flags) {
    gs_gfx_texture h = { 0 };
    if (!gfx_need_device("load_texture")) return h;
    char buf[1024];
    size_t n = path.len < 0 ? 0 : (size_t)path.len;
    if (n >= sizeof buf) n = sizeof buf - 1;
    if (n) memcpy(buf, path.data, n);
    buf[n] = 0;
    bool bmp = n >= 4 && SDL_strcasecmp(buf + n - 4, ".bmp") == 0;
    SDL_Surface *loaded = bmp ? SDL_LoadBMP(buf) : SDL_LoadPNG(buf);
    if (!loaded) {
        gfx_fail("cannot load %s: %s", buf, SDL_GetError());
        return h;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) {
        gfx_sdl_fail("converting an image");
        return h;
    }
    gs_gfx_texture_desc d;
    SDL_zero(d);
    d.kind = GS_GFX_TEXTURE_2D;
    d.format = (flags & GS_GFX_LOAD_SRGB) ? GS_GFX_RGBA8_SRGB : GS_GFX_RGBA8;
    d.usage = GS_GFX_SAMPLED;
    d.width = rgba->w;
    d.height = rgba->h;
    d.depth = 1;
    d.mips = (flags & GS_GFX_LOAD_MIPS) ? 0 : 1;
    d.samples = 1;
    h = gs_gfx_create_texture(&d);
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, h.id);
    if (s) {
        /* Rows as the surface has them, which may be padded. */
        size_t row = (size_t)rgba->w * 4;
        uint8_t *tight = (uint8_t *)malloc(row * (size_t)rgba->h);
        for (int y = 0; y < rgba->h; y++)
            memcpy(tight + row * (size_t)y, (uint8_t *)rgba->pixels + (size_t)rgba->pitch * (size_t)y,
                   row);
        gs_gfx_region r = { 0 };
        SDL_GPUTextureRegion reg;
        gfx_region(s, &r, &reg, "load_texture");
        bool ok = gfx_upload_texture(&reg, s->format, tight, true, "load_texture");
        free(tight);
        if (ok && s->desc.mips > 1) {
            /* After the upload, in the same command buffer. */
            SDL_GPUCommandBuffer *cb = gfx_fresh_cmd();
            if (cb) SDL_GenerateMipmapsForGPUTexture(cb, s->tex);
        }
        if (!ok) {
            gs_gfx_release_texture(h);
            h.id = 0;
        }
    }
    SDL_DestroySurface(rgba);
    return h;
}

void gs_gfx_generate_mips(gs_gfx_texture t) {
    if (!gfx_need_device("generate_mips")) return;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return;
    if (s->desc.mips < 2 || !(s->usage & SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        gfx_misuse("generate_mips: a sampled texture with more than one mip");
        return;
    }
    if (gfx_block(s->format) > 1) {
        gfx_misuse("generate_mips: a compressed texture; upload its mips");
        return;
    }
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, "generate_mips");
    if (cb) SDL_GenerateMipmapsForGPUTexture(cb, s->tex);
}

uint8_t gs_gfx_texture_info(gs_gfx_texture t, gs_gfx_texture_desc *out) {
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, t.id);
    if (!s) return 0;
    *out = s->desc;
    return 1;
}

void gs_gfx_release_texture(gs_gfx_texture t) {
    if (!gfx_need_device("release_texture")) return;
    if (t.id == gfx.screen.id || t.id == gfx.screen_depth.id) {
        gfx_misuse("release_texture: the screen belongs to gfx");
        return;
    }
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return;
    SDL_ReleaseGPUTexture(gfx.dev, s->tex);
    gfx_table_remove(&gfx.textures, t.id);
}

/* --- samplers -------------------------------------------------------------- */

gs_gfx_sampler gs_gfx_create_sampler(const gs_gfx_sampler_desc *desc) {
    gs_gfx_sampler h = { 0 };
    if (!gfx_need_device("sampler")) return h;
    const gs_gfx_sampler_desc *d = desc;
    if ((uint32_t)d->min_filter > 1 || (uint32_t)d->mag_filter > 1 || (uint32_t)d->mip_filter > 1 ||
        (uint32_t)d->wrap_u > 2 || (uint32_t)d->wrap_v > 2 || (uint32_t)d->wrap_w > 2 ||
        (uint32_t)d->compare > 8) {
        gfx_misuse("a sampler with filters, wraps or a compare that are not gfx's constants");
        return h;
    }
    if (!(d->min_lod >= 0.0f && d->max_lod >= d->min_lod)) {
        gfx_misuse("a sampler with min_lod %g and max_lod %g: 0 <= min_lod <= max_lod",
                   (double)d->min_lod, (double)d->max_lod);
        return h;
    }
    SDL_GPUSamplerCreateInfo ci;
    SDL_zero(ci);
    ci.min_filter = (SDL_GPUFilter)d->min_filter;
    ci.mag_filter = (SDL_GPUFilter)d->mag_filter;
    ci.mipmap_mode = (SDL_GPUSamplerMipmapMode)d->mip_filter;
    ci.address_mode_u = (SDL_GPUSamplerAddressMode)d->wrap_u;
    ci.address_mode_v = (SDL_GPUSamplerAddressMode)d->wrap_v;
    ci.address_mode_w = (SDL_GPUSamplerAddressMode)d->wrap_w;
    ci.max_anisotropy = d->max_anisotropy;
    ci.enable_anisotropy = d->max_anisotropy > 1.0f;
    ci.compare_op = (SDL_GPUCompareOp)d->compare;
    ci.enable_compare = d->compare != 0;
    ci.min_lod = d->min_lod;
    ci.max_lod = d->max_lod;
    ci.mip_lod_bias = d->lod_bias;
    SDL_GPUSampler *smp = SDL_CreateGPUSampler(gfx.dev, &ci);
    if (!smp) {
        gfx_sdl_fail("creating a sampler");
        return h;
    }
    gfx_sampler_slot *s = (gfx_sampler_slot *)gfx_table_add(&gfx.samplers, &h.id);
    if (!s) {
        SDL_ReleaseGPUSampler(gfx.dev, smp);
        return h;
    }
    s->smp = smp;
    return h;
}

void gs_gfx_release_sampler(gs_gfx_sampler smp) {
    if (!gfx_need_device("release_sampler")) return;
    gfx_sampler_slot *s = (gfx_sampler_slot *)gfx_table_get(&gfx.samplers, smp.id);
    if (!s) return;
    SDL_ReleaseGPUSampler(gfx.dev, s->smp);
    gfx_table_remove(&gfx.samplers, smp.id);
}

/* --- teardown -------------------------------------------------------------- */

static void gfx_free_buffer(void *item) {
    SDL_ReleaseGPUBuffer(gfx.dev, ((gfx_buffer_slot *)item)->buf);
}

static void gfx_free_texture(void *item) {
    SDL_ReleaseGPUTexture(gfx.dev, ((gfx_texture_slot *)item)->tex);
}

static void gfx_free_sampler(void *item) {
    SDL_ReleaseGPUSampler(gfx.dev, ((gfx_sampler_slot *)item)->smp);
}

static void gfx_free_readback(void *item) { gfx_readback_free((gfx_readback_slot *)item); }

void gfx_release_resources(void) {
    gfx_table_each(&gfx.buffers, gfx_free_buffer);
    gfx_table_each(&gfx.textures, gfx_free_texture);
    gfx_table_each(&gfx.samplers, gfx_free_sampler);
    gfx_table_each(&gfx.readbacks, gfx_free_readback);
    gfx_table_clear(&gfx.buffers);
    gfx_table_clear(&gfx.readbacks);
    gfx_table_clear(&gfx.textures);
    gfx_table_clear(&gfx.samplers);
}
