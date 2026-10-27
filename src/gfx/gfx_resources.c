/* The gfx layer: buffers, textures and samplers, and moving data between
   them and the program. */

#include "webp/decode.h"
#include "gfx_internal.h"

#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif

/* Pages for a decoded picture that lives only until its upload. macOS's malloc
   keeps a freed large block dirty in the process (Hana's rig atlas is 47 MiB),
   so a picture gets pages of its own, handed back whole when it is freed. */
static void *gfx_pages(size_t n) {
#if defined(_WIN32)
    return VirtualAlloc(NULL, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(__unix__) || defined(__APPLE__)
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#else
    return malloc(n);
#endif
}

static void gfx_free_pages(void *p, size_t n) {
#if defined(_WIN32)
    (void)n;
    VirtualFree(p, 0, MEM_RELEASE);
#elif defined(__unix__) || defined(__APPLE__)
    munmap(p, n);
#else
    (void)n;
    free(p);
#endif
}

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
        case GS_GFX_ASTC_4x4: *out = SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM; return true;
        case GS_GFX_ASTC_4x4_SRGB: *out = SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB; return true;
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

/* Whether the device has 2D textures of a format with a usage: for a
   program choosing among formats, as among compressed ones, before it
   creates one. */
uint8_t gs_gfx_texture_format_supported(int32_t format, int64_t usage) {
    SDL_GPUTextureFormat f;
    if (!gfx.dev || !gfx_texture_format(format, &f) || !usage || (usage & ~127)) return 0;
    return SDL_GPUTextureSupportsFormat(gfx.dev, f, SDL_GPU_TEXTURETYPE_2D,
                                        (SDL_GPUTextureUsageFlags)usage);
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
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM:
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB:
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

/* Where a copy is recorded. A new resource's first contents go through a
   command buffer of their own, submitted at once: it can be created
   anywhere, even inside a pass, and nothing earlier can be using it. Any
   other copy goes into the command buffer being recorded, in program order
   with the draws around it, and so cannot happen inside a pass. */
static SDL_GPUCommandBuffer *gfx_copy_cmd(bool fresh, const char *what) {
    if (fresh) {
        SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(gfx.dev);
        if (!cb) gfx_sdl_fail("acquiring a command buffer");
        return cb;
    }
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("%s inside a %s pass: copies happen between passes", what,
                   gfx.pass ? "render" : "compute");
        return NULL;
    }
    return gfx_cmd();
}

static bool gfx_copy_done(SDL_GPUCommandBuffer *cb, bool fresh) {
    if (!fresh) return true;
    return SDL_SubmitGPUCommandBuffer(cb) || gfx_sdl_fail("submitting an upload");
}

/* Writes `len` bytes of `data` and then `zeros` zero bytes into `buf` at
   `offset`. `cycle` lets SDL give the buffer fresh memory when a draw still
   pending uses the old, for a rewrite of the whole buffer only. */
static bool gfx_upload_buffer(SDL_GPUBuffer *buf, uint32_t offset, const void *data,
                              uint32_t len, uint32_t zeros, bool fresh, bool cycle,
                              const char *what) {
    uint32_t total = len + zeros;
    if (!total) return true;
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(fresh, what);
    if (!cb) return false;
    SDL_GPUTransferBuffer *tb = gfx_transfer(total, true);
    if (!tb) {
        if (fresh) SDL_CancelGPUCommandBuffer(cb);
        return false;
    }
    uint8_t *p = (uint8_t *)SDL_MapGPUTransferBuffer(gfx.dev, tb, false);
    if (len) memcpy(p, data, len);
    if (zeros) memset(p + len, 0, zeros);
    SDL_UnmapGPUTransferBuffer(gfx.dev, tb);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTransferBufferLocation src = { tb, 0 };
    SDL_GPUBufferRegion dst = { buf, offset, total };
    SDL_UploadToGPUBuffer(cp, &src, &dst, cycle);
    SDL_EndGPUCopyPass(cp);
    SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
    return gfx_copy_done(cb, fresh);
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
    if (!gfx_cmd()) return 0;
    return gfx_upload_buffer(s->buf, (uint32_t)offset, data.data, (uint32_t)data.len, 0, false,
                             whole && gfx_cycle_once(&s->cycled_in), "update_buffer");
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

static bool gfx_upload_texture(const SDL_GPUTextureRegion *reg, SDL_GPUTextureFormat format,
                               const void *data, uint32_t len, bool fresh, const char *what) {
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(fresh, what);
    if (!cb) return false;
    SDL_GPUTransferBuffer *tb = gfx_transfer(len, true);
    if (!tb) {
        if (fresh) SDL_CancelGPUCommandBuffer(cb);
        return false;
    }
    void *p = SDL_MapGPUTransferBuffer(gfx.dev, tb, false);
    memcpy(p, data, len);
    SDL_UnmapGPUTransferBuffer(gfx.dev, tb);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo src;
    SDL_zero(src);
    src.transfer_buffer = tb;
    gfx_transfer_layout(&src, reg, format);
    SDL_UploadToGPUTexture(cp, &src, reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
    return gfx_copy_done(cb, fresh);
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
    return gfx_upload_texture(&reg, s->format, data.data, (uint32_t)data.len, false, "update_texture");
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

// A JPEG's size, from its first start-of-frame marker; false if it has none.
static bool gfx_jpeg_size(const uint8_t *d, size_t n, int *w, int *h) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < n) {
        if (d[i] != 0xFF) return false;
        uint8_t m = d[i + 1];
        if (m == 0xFF) { i++; continue; }
        size_t len = (size_t)d[i + 2] << 8 | d[i + 3];
        // SOF0..SOF15, less DHT (C4), JPG (C8) and DAC (CC)
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            *h = d[i + 5] << 8 | d[i + 6];
            *w = d[i + 7] << 8 | d[i + 8];
            return *w > 0 && *h > 0;
        }
        i += 2 + len;
    }
    return false;
}

// A JPEG file as an RGBA surface, decoded by SDL's own stb_image (SDL_ConvertPixels from
// MJPG), or NULL with SDL's error set.
static SDL_Surface *gfx_load_jpeg(const char *path) {
    size_t size = 0;
    void *file = SDL_LoadFile(path, &size);
    if (!file) return NULL;
    int w = 0, h = 0;
    SDL_Surface *s = NULL;
    if (!gfx_jpeg_size((const uint8_t *)file, size, &w, &h) || size > INT32_MAX) {
        SDL_SetError("not a JPEG image");
    } else {
        s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        if (s && !SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_MJPG, file, (int)size, SDL_PIXELFORMAT_RGBA32,
                                    s->pixels, s->pitch)) {
            SDL_DestroySurface(s);
            s = NULL;
        }
    }
    SDL_free(file);
    return s;
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
    bool webp = n >= 5 && SDL_strcasecmp(buf + n - 5, ".webp") == 0;
    bool jpeg = (n >= 4 && SDL_strcasecmp(buf + n - 4, ".jpg") == 0) ||
                (n >= 5 && SDL_strcasecmp(buf + n - 5, ".jpeg") == 0);
    /* The pixels as tight RGBA rows: a WebP straight from libwebp's decoder, anything else
       through an SDL surface, converted only when it is not RGBA already. */
    uint8_t *px = NULL;
    int w = 0, ht = 0;
    size_t bytes = 0;
    SDL_Surface *surf = NULL;
    if (webp) {
        size_t size = 0;
        void *file = SDL_LoadFile(buf, &size);
        const char *why = file ? "not a WebP image" : SDL_GetError();
        if (file && WebPGetInfo((const uint8_t *)file, size, &w, &ht)) {
            bytes = (size_t)w * 4 * (size_t)ht;
            px = (uint8_t *)gfx_pages(bytes);
            if (!px) why = "out of memory";
            else if (!WebPDecodeRGBAInto((const uint8_t *)file, size, px, bytes, w * 4)) {
                gfx_free_pages(px, bytes);
                px = NULL;
            }
        }
        SDL_free(file);
        if (!px) {
            gfx_fail("cannot load %s: %s", buf, why);
            return h;
        }
    } else {
        surf = bmp ? SDL_LoadBMP(buf) : jpeg ? gfx_load_jpeg(buf) : SDL_LoadPNG(buf);
        if (!surf) {
            gfx_fail("cannot load %s: %s", buf, SDL_GetError());
            return h;
        }
        if (surf->format != SDL_PIXELFORMAT_RGBA32) {
            SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(surf);
            surf = rgba;
            if (!surf) {
                gfx_sdl_fail("converting an image");
                return h;
            }
        }
        w = surf->w;
        ht = surf->h;
        if (surf->pitch == w * 4) {
            px = (uint8_t *)surf->pixels;
        } else {
            px = (uint8_t *)malloc((size_t)w * 4 * (size_t)ht);
            for (int y = 0; y < ht; y++)
                memcpy(px + (size_t)w * 4 * (size_t)y, (uint8_t *)surf->pixels + (size_t)surf->pitch * (size_t)y,
                       (size_t)w * 4);
        }
    }
    gs_gfx_texture_desc d;
    SDL_zero(d);
    d.kind = GS_GFX_TEXTURE_2D;
    d.format = (flags & GS_GFX_LOAD_SRGB) ? GS_GFX_RGBA8_SRGB : GS_GFX_RGBA8;
    d.usage = GS_GFX_SAMPLED;
    d.width = w;
    d.height = ht;
    d.depth = 1;
    d.mips = (flags & GS_GFX_LOAD_MIPS) ? 0 : 1;
    d.samples = 1;
    h = gs_gfx_create_texture(&d);
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, h.id);
    if (s) {
        gs_gfx_region r = { 0 };
        SDL_GPUTextureRegion reg;
        gfx_region(s, &r, &reg, "load_texture");
        bool ok = gfx_upload_texture(&reg, s->format, px, (uint32_t)((size_t)w * 4 * (size_t)ht), true,
                                     "load_texture");
        if (ok && s->desc.mips > 1) {
            SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(gfx.dev);
            if (cb) {
                SDL_GenerateMipmapsForGPUTexture(cb, s->tex);
                SDL_SubmitGPUCommandBuffer(cb);
            }
        }
        if (!ok) {
            gs_gfx_release_texture(h);
            h.id = 0;
        }
    }
    if (webp) gfx_free_pages(px, bytes);
    else {
        if (px != surf->pixels) free(px);
        SDL_DestroySurface(surf);
    }
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

/* --- pictures decoded later ------------------------------------------------- */
/* load_image_later queues a WebP file for the package's loading threads, which
   decode it into pages of its own with its edge pixels repeated border px
   round it (so a linear sampler at the picture's edge reads only the picture);
   image_later_state, on the program's thread, uploads a decoded one into its
   texture's rect and frees its pixels. Only that upload (a copy into a
   transfer buffer, then a copy pass in order with the frame's work) is on the
   program's thread, so a frame that asks for pictures does not decode them.
   image_size_later reads only a WebP's size, from its header, on the same
   threads (a book's pictures sized before they are packed, with no file
   opened on the program's thread); image_size_state gives it. */
#define GFX_LOADERS 2
typedef struct gfx_later {
    struct gfx_later *next;
    int64_t id;
    char path[1024];
    gs_gfx_texture t;
    int32_t x, y, w, h, border;
    bool size_only;  /* image_size_later: its size read into w and h, no pixels */
    int state;    /* 0 queued, 1 decoding, 2 decoded, 3 failed */
    uint8_t *px;  /* (w + 2 border) x (h + 2 border) RGBA rows, from gfx_pages */
    size_t bytes;
    char why[1280];
} gfx_later;

static struct {
    SDL_Mutex *lock;
    SDL_Condition *wake;
    SDL_Thread *threads[GFX_LOADERS];
    gfx_later *jobs;  /* every job not yet taken by image_later_state, oldest first */
    int64_t next_id;  /* never reset: a job's number is never another's */
    bool quit;
} gfx_laters;

/* The picture at path, w x h, decoded with its edge pixels border px round it;
   NULL with why set. Any thread. */
static uint8_t *gfx_decode_bordered(const char *path, int32_t w, int32_t h, int32_t b,
                                    size_t *bytes_out, char *why, size_t n) {
    size_t size = 0;
    void *file = SDL_LoadFile(path, &size);
    if (!file) {
        snprintf(why, n, "cannot load %s: %s", path, SDL_GetError());
        return NULL;
    }
    uint8_t *px = NULL;
    int fw = 0, fh = 0;
    if (!WebPGetInfo((const uint8_t *)file, size, &fw, &fh)) {
        snprintf(why, n, "cannot load %s: not a WebP image", path);
    } else if (fw != w || fh != h) {
        snprintf(why, n, "%s is %d x %d, not %d x %d", path, fw, fh, w, h);
    } else {
        size_t stride = (size_t)(w + 2 * b) * 4, rows = (size_t)(h + 2 * b);
        size_t bytes = stride * rows, at = stride * (size_t)b + (size_t)b * 4;
        px = (uint8_t *)gfx_pages(bytes);
        if (!px) {
            snprintf(why, n, "cannot load %s: out of memory", path);
        } else if (!WebPDecodeRGBAInto((const uint8_t *)file, size, px + at, bytes - at, (int)stride)) {
            snprintf(why, n, "cannot load %s: not a WebP image", path);
            gfx_free_pages(px, bytes);
            px = NULL;
        } else {
            /* each row's first and last pixels out to the sides, then the first
               and last rows whole (the corners with them) above and below */
            for (int32_t y = b; y < b + h; y++) {
                uint8_t *row = px + stride * (size_t)y;
                for (int32_t k = 0; k < b; k++) {
                    memcpy(row + (size_t)k * 4, row + (size_t)b * 4, 4);
                    memcpy(row + (size_t)(b + w + k) * 4, row + (size_t)(b + w - 1) * 4, 4);
                }
            }
            for (int32_t k = 0; k < b; k++) {
                memcpy(px + stride * (size_t)k, px + stride * (size_t)b, stride);
                memcpy(px + stride * (size_t)(b + h + k), px + stride * (size_t)(b + h - 1), stride);
            }
            *bytes_out = bytes;
        }
    }
    SDL_free(file);
    return px;
}

/* A WebP's size from its first 30 bytes (the RIFF header and its first
   chunk's: VP8X's canvas, VP8L's or VP8's frame); false with why set. Any
   thread. */
static bool gfx_webp_size(const char *path, int32_t *w, int32_t *h, char *why, size_t n) {
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) {
        snprintf(why, n, "cannot load %s: %s", path, SDL_GetError());
        return false;
    }
    uint8_t b[30];
    size_t got = SDL_ReadIO(io, b, sizeof b);
    SDL_CloseIO(io);
    *w = *h = 0;
    if (got == sizeof b && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBPVP8", 7) == 0) {
        if (b[15] == 'X') {
            *w = 1 + (b[24] | b[25] << 8 | b[26] << 16);
            *h = 1 + (b[27] | b[28] << 8 | b[29] << 16);
        } else if (b[15] == 'L') {
            uint32_t v = (uint32_t)b[21] | (uint32_t)b[22] << 8 | (uint32_t)b[23] << 16 | (uint32_t)b[24] << 24;
            *w = 1 + (int32_t)(v & 0x3fff);
            *h = 1 + (int32_t)((v >> 14) & 0x3fff);
        } else if (b[15] == ' ') {
            *w = (b[26] | b[27] << 8) & 0x3fff;
            *h = (b[28] | b[29] << 8) & 0x3fff;
        }
    }
    if (*w > 0 && *h > 0) return true;
    snprintf(why, n, "cannot load %s: not a WebP image", path);
    return false;
}

static int SDLCALL gfx_later_loader(void *user) {
    (void)user;
    for (;;) {
        SDL_LockMutex(gfx_laters.lock);
        gfx_later *j = NULL;
        while (!gfx_laters.quit) {
            for (j = gfx_laters.jobs; j && j->state != 0; j = j->next) {}
            if (j) break;
            SDL_WaitCondition(gfx_laters.wake, gfx_laters.lock);
        }
        if (gfx_laters.quit) {
            SDL_UnlockMutex(gfx_laters.lock);
            return 0;
        }
        /* the job stays in the list, owned by this thread, until it is marked
           decoded: only image_later_state takes a job out, and only then */
        j->state = 1;
        SDL_UnlockMutex(gfx_laters.lock);
        size_t bytes = 0;
        char why[sizeof j->why];
        why[0] = 0;
        int32_t w = 0, h = 0;
        bool ok = false;
        uint8_t *px = NULL;
        if (j->size_only) ok = gfx_webp_size(j->path, &w, &h, why, sizeof why);
        else ok = (px = gfx_decode_bordered(j->path, j->w, j->h, j->border, &bytes, why, sizeof why)) != NULL;
        SDL_LockMutex(gfx_laters.lock);
        j->px = px;
        j->bytes = bytes;
        if (j->size_only) { j->w = w; j->h = h; }
        memcpy(j->why, why, sizeof why);
        j->state = ok ? 2 : 3;
        SDL_UnlockMutex(gfx_laters.lock);
        gs_gfx_wake();
    }
}

/* The loading threads stopped and every job dropped (gs_gfx_close). */
void gfx_later_stop(void) {
    if (!gfx_laters.lock) return;
    SDL_LockMutex(gfx_laters.lock);
    gfx_laters.quit = true;
    SDL_BroadcastCondition(gfx_laters.wake);
    SDL_UnlockMutex(gfx_laters.lock);
    for (int i = 0; i < GFX_LOADERS; i++) {
        if (gfx_laters.threads[i]) SDL_WaitThread(gfx_laters.threads[i], NULL);
        gfx_laters.threads[i] = NULL;
    }
    while (gfx_laters.jobs) {
        gfx_later *next = gfx_laters.jobs->next;
        if (gfx_laters.jobs->px) gfx_free_pages(gfx_laters.jobs->px, gfx_laters.jobs->bytes);
        free(gfx_laters.jobs);
        gfx_laters.jobs = next;
    }
    gfx_laters.quit = false;
}

/* A job for path, its loading threads started; NULL with the error set. */
static gfx_later *gfx_later_new(gs_gfx_bytes path, const char *what) {
    if (path.len <= 0 || (size_t)path.len >= sizeof ((gfx_later *)0)->path)
        return gfx_fail("%s: a path of %lld bytes", what, (long long)path.len), NULL;
    if (!gfx_laters.lock && !(gfx_laters.lock = SDL_CreateMutex())) return gfx_sdl_fail(what), NULL;
    if (!gfx_laters.wake && !(gfx_laters.wake = SDL_CreateCondition())) return gfx_sdl_fail(what), NULL;
    for (int i = 0; i < GFX_LOADERS; i++)
        if (!gfx_laters.threads[i] &&
            !(gfx_laters.threads[i] = SDL_CreateThread(gfx_later_loader, "goose gfx loader", NULL)))
            return gfx_sdl_fail(what), NULL;
    gfx_later *j = (gfx_later *)calloc(1, sizeof *j);
    if (!j) return gfx_fail("%s: out of memory", what), NULL;
    memcpy(j->path, path.data, (size_t)path.len);
    return j;
}

/* Job j queued for the loading threads; its number. */
static int64_t gfx_later_queue(gfx_later *j) {
    SDL_LockMutex(gfx_laters.lock);
    j->id = ++gfx_laters.next_id;
    gfx_later **end = &gfx_laters.jobs;
    while (*end) end = &(*end)->next;
    *end = j;
    SDL_SignalCondition(gfx_laters.wake);
    SDL_UnlockMutex(gfx_laters.lock);
    return j->id;
}

/* Job number job taken out of the list once it is done (the caller frees it);
   GFX_LATER_BUSY while it is not; NULL, with the error set, for no such job. */
static gfx_later gfx_later_busy;
#define GFX_LATER_BUSY (&gfx_later_busy)
static gfx_later *gfx_later_take(int64_t job, const char *what) {
    gfx_later *j = NULL, **at = NULL;
    if (gfx_laters.lock) {
        SDL_LockMutex(gfx_laters.lock);
        for (at = &gfx_laters.jobs; (j = *at) != NULL; at = &j->next)
            if (j->id == job) break;
        if (j && j->state < 2) j = GFX_LATER_BUSY;
        else if (j) *at = j->next;
        SDL_UnlockMutex(gfx_laters.lock);
    }
    if (!j) gfx_fail("%s: no job %lld", what, (long long)job);
    return j;
}

int64_t gs_gfx_load_image_later(gs_gfx_bytes path, gs_gfx_texture t, int32_t x, int32_t y, int32_t w,
                                int32_t h, int32_t border) {
    const char *what = "load_image_later";
    if (!gfx_need_device(what)) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return 0;
    if (s->format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM && s->format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)
        return gfx_misuse("%s: an RGBA8 texture is needed", what), 0;
    if (w <= 0 || h <= 0 || border < 0)
        return gfx_misuse("%s: a %d x %d picture with a border of %d", what, w, h, border), 0;
    gs_gfx_region r = { 0 };
    r.x = x - border;
    r.y = y - border;
    r.w = w + 2 * border;
    r.h = h + 2 * border;
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, &r, &reg, what)) return 0;
    gfx_later *j = gfx_later_new(path, what);
    if (!j) return 0;
    j->t = t;
    j->x = x;
    j->y = y;
    j->w = w;
    j->h = h;
    j->border = border;
    return gfx_later_queue(j);
}

int64_t gs_gfx_image_size_later(gs_gfx_bytes path) {
    gfx_later *j = gfx_later_new(path, "image_size_later");
    if (!j) return 0;
    j->size_only = true;
    return gfx_later_queue(j);
}

int64_t gs_gfx_image_size_state(int64_t job) {
    gfx_later *j = gfx_later_take(job, "image_size_state");
    if (!j) return -1;
    if (j == GFX_LATER_BUSY) return 0;
    int64_t r = -1;
    if (!j->size_only) gfx_fail("image_size_state: job %lld is a picture's, not a size", (long long)job);
    else if (j->state == 3) gfx_fail("%s", j->why);
    else r = (int64_t)j->w * 65536 + j->h;
    if (j->px) gfx_free_pages(j->px, j->bytes);
    free(j);
    return r;
}

int32_t gs_gfx_image_later_state(int64_t job) {
    const char *what = "image_later_state";
    gfx_later *j = gfx_later_take(job, what);
    if (!j) return -1;
    if (j == GFX_LATER_BUSY) return 0;
    int32_t state = 1;
    if (j->size_only) {
        gfx_fail("%s: job %lld is a size's, not a picture's", what, (long long)job);
        state = -1;
    } else if (j->state == 3) {
        gfx_fail("%s", j->why);
        state = -1;
    } else {
        gfx_texture_slot *s = gfx_need_device(what) ? gfx_texture_slot_of(j->t) : NULL;
        gs_gfx_region r = { 0 };
        r.x = j->x - j->border;
        r.y = j->y - j->border;
        r.w = j->w + 2 * j->border;
        r.h = j->h + 2 * j->border;
        SDL_GPUTextureRegion reg;
        if (!s || !gfx_region(s, &r, &reg, what) ||
            !gfx_upload_texture(&reg, s->format, j->px, (uint32_t)j->bytes, false, what))
            state = -1;
    }
    if (j->px) gfx_free_pages(j->px, j->bytes);
    free(j);
    return state;
}

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
