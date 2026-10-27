/* The gfx layer: the device, the window and its screen, frames, input and
   time, plus the handle tables and error reporting the other files share. */

#include "gfx_internal.h"

#include <vulkan/vulkan_core.h>
#include <stdarg.h>
#include <stdlib.h>

gfx_state gfx;

/* How many times gfx was opened, which close() leaves: handles from an
   earlier open are gone once it changes. */
static int64_t gfx_opens;

/* The user event wake() pushes, 0 while gfx is closed: an atomic, since
   wake() may come from any thread. */
static SDL_AtomicU32 gfx_wake_event;

/* --- errors ---------------------------------------------------------------- */

static void gfx_vset(const char *fmt, va_list args) {
    vsnprintf(gfx.error, sizeof gfx.error, fmt, args);
}

bool gfx_fail(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gfx_vset(fmt, args);
    va_end(args);
    return false;
}

bool gfx_misuse(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gfx_vset(fmt, args);
    va_end(args);
    /* The Goose side aborts with the latest at the next frame; the first few
       are shown as they happen, since a misuse often causes others. */
    if (++gfx.misuse <= 8) fprintf(stderr, "gfx: %s\n", gfx.error);
    return false;
}

bool gfx_sdl_fail(const char *what) {
    return gfx_fail("%s: %s", what, SDL_GetError());
}

bool gfx_need_device(const char *what) {
    if (gfx.dev) return true;
    return gfx_misuse("%s: gfx is not open (call gfx::open or gfx::open_headless first)", what);
}

/* SDL_GPU checks its own usage with release asserts, whose default handler
   shows a dialog: a misuse here instead, and the call is skipped. */
static SDL_AssertState SDLCALL gfx_assert_handler(const SDL_AssertData *data, void *user) {
    (void)user;
    gfx_misuse("SDL: %s (%s:%d)", data->condition, data->filename, data->linenum);
    return SDL_ASSERTION_IGNORE;
}

int64_t gs_gfx_error(gs_gfx_bytes out) {
    int64_t n = (int64_t)strlen(gfx.error);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, gfx.error, (size_t)k);
    return n;
}

int64_t gs_gfx_misuse_count(void) { return gfx.misuse; }

/* --- handle tables --------------------------------------------------------- */

void *gfx_table_add(gfx_table *t, uint32_t *id) {
    uint32_t i;
    if (t->nfree) {
        i = t->freelist[--t->nfree];
    } else {
        if (t->count == t->cap) {
            uint32_t cap = t->cap ? t->cap * 2 : 64;
            if (cap > GFX_INDEX_MASK) {
                gfx_fail("too many %ss", t->what);
                return NULL;
            }
            t->items = (unsigned char *)realloc(t->items, (size_t)cap * t->item_size);
            t->gens = (uint16_t *)realloc(t->gens, cap * sizeof(uint16_t));
            t->live = (uint8_t *)realloc(t->live, cap);
            t->freelist = (uint32_t *)realloc(t->freelist, cap * sizeof(uint32_t));
            t->cap = cap;
        }
        i = t->count++;
        t->gens[i] = 0;
    }
    t->gens[i] = (uint16_t)(t->gens[i] % 4095 + 1);
    t->live[i] = 1;
    void *item = t->items + (size_t)i * t->item_size;
    memset(item, 0, t->item_size);
    *id = ((uint32_t)t->gens[i] << GFX_INDEX_BITS) | i;
    return item;
}

void *gfx_table_find(gfx_table *t, uint32_t id) {
    uint32_t i = id & GFX_INDEX_MASK;
    if (!id || i >= t->count || !t->live[i] || t->gens[i] != id >> GFX_INDEX_BITS) return NULL;
    return t->items + (size_t)i * t->item_size;
}

void *gfx_table_get(gfx_table *t, uint32_t id) {
    void *item = gfx_table_find(t, id);
    if (!item) {
        if (!id) gfx_misuse("a null %s handle", t->what);
        else gfx_misuse("a %s handle that was released, or never created", t->what);
    }
    return item;
}

void gfx_table_remove(gfx_table *t, uint32_t id) {
    if (!gfx_table_find(t, id)) return;
    uint32_t i = id & GFX_INDEX_MASK;
    t->live[i] = 0;
    t->freelist[t->nfree++] = i;
}

void gfx_table_each(gfx_table *t, void (*f)(void *item)) {
    for (uint32_t i = 0; i < t->count; i++)
        if (t->live[i]) f(t->items + (size_t)i * t->item_size);
}

void gfx_table_clear(gfx_table *t) {
    free(t->items);
    free(t->gens);
    free(t->live);
    free(t->freelist);
    const char *what = t->what;
    uint32_t size = t->item_size;
    memset(t, 0, sizeof *t);
    t->what = what;
    t->item_size = size;
}

static void gfx_init_tables(void) {
    gfx.buffers.what = "buffer";
    gfx.buffers.item_size = sizeof(gfx_buffer_slot);
    gfx.textures.what = "texture";
    gfx.textures.item_size = sizeof(gfx_texture_slot);
    gfx.samplers.what = "sampler";
    gfx.samplers.item_size = sizeof(gfx_sampler_slot);
    gfx.pipelines.what = "pipeline";
    gfx.pipelines.item_size = sizeof(gfx_pipeline_slot);
    gfx.computes.what = "compute pipeline";
    gfx.computes.item_size = sizeof(gfx_compute_slot);
    gfx.readbacks.what = "readback";
    gfx.readbacks.item_size = sizeof(gfx_readback_slot);
}

/* --- command buffers ------------------------------------------------------- */

/* The accumulator of `frame`'s timings, made if `make`; the oldest goes
   when there is no room. */
static int gfx_timed_frame(int64_t frame, bool make) {
    for (int i = 0; i < gfx.ntimed_frames; i++)
        if (gfx.timed_frames[i].frame == frame) return i;
    if (!make) return -1;
    if (gfx.ntimed_frames == GFX_MAX_TIMED_FRAMES) {
        memmove(gfx.timed_frames, gfx.timed_frames + 1,
                sizeof gfx.timed_frames[0] * (GFX_MAX_TIMED_FRAMES - 1));
        gfx.ntimed_frames--;
    }
    int i = gfx.ntimed_frames++;
    memset(&gfx.timed_frames[i], 0, sizeof gfx.timed_frames[i]);
    gfx.timed_frames[i].frame = frame;
    return i;
}

static SDL_GPUCommandBuffer *gfx_acquire_cmd(void) {
    if (!gfx.timing) return SDL_AcquireGPUCommandBuffer(gfx.dev);
    int f = gfx_timed_frame(gfx.frames, true);
    if (gfx.ntimed == GFX_MAX_TIMED) {
        gfx.timed_frames[f].bad = true;
        return SDL_AcquireGPUCommandBuffer(gfx.dev);
    }
    Uint64 id = 0;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUTimedCommandBuffer(gfx.dev, &id);
    if (cb) {
        gfx.timed[gfx.ntimed].id = id;
        gfx.timed[gfx.ntimed].frame = gfx.frames;
        gfx.ntimed++;
        gfx.timed_frames[f].pending++;
    }
    return cb;
}

/* Takes in the timings that have arrived, and makes gpu_time the newest
   finished frame's whose timings have all arrived. */
void gfx_poll_timing(void) {
    if (!gfx.timing) return;
    for (;;) {
        SDL_GPUCommandBufferTimingResult results[16];
        Uint32 n = 0;
        if (!SDL_PollGPUCommandBufferTiming(gfx.dev, results, 16, &n) || n == 0) break;
        for (Uint32 r = 0; r < n; r++) {
            int t = 0;
            while (t < gfx.ntimed && gfx.timed[t].id != results[r].sample_id) t++;
            if (t == gfx.ntimed) continue;
            int f = gfx_timed_frame(gfx.timed[t].frame, false);
            gfx.timed[t] = gfx.timed[--gfx.ntimed];
            if (f < 0) continue;
            gfx.timed_frames[f].pending--;
            if (results[r].status == SDL_GPU_COMMANDBUFFER_TIMING_STATUS_OK)
                gfx.timed_frames[f].ns += results[r].duration_ns;
            else
                gfx.timed_frames[f].bad = true;
        }
    }
    for (int i = 0; i < gfx.ntimed_frames;) {
        if (gfx.timed_frames[i].frame >= gfx.frames || gfx.timed_frames[i].pending > 0) {
            i++;
            continue;
        }
        if (!gfx.timed_frames[i].bad && gfx.timed_frames[i].frame > gfx.gpu_time_frame) {
            gfx.gpu_time = (double)gfx.timed_frames[i].ns / 1e9;
            gfx.gpu_time_frame = gfx.timed_frames[i].frame;
        }
        memmove(gfx.timed_frames + i, gfx.timed_frames + i + 1,
                sizeof gfx.timed_frames[0] * (size_t)(gfx.ntimed_frames - i - 1));
        gfx.ntimed_frames--;
    }
}

double gs_gfx_gpu_time(void) {
    if (!gfx.dev) return -1.0;
    gfx.time_wanted = true;
    return gfx.gpu_time;
}

SDL_GPUCommandBuffer *gfx_cmd(void) {
    if (!gfx.cmd) {
        gfx.cmd = gfx_acquire_cmd();
        if (!gfx.cmd) gfx_sdl_fail("acquiring a command buffer");
        gfx.cmd_serial++;
        memset(gfx.uniforms_pushed, 0, sizeof gfx.uniforms_pushed);
        for (int i = 0; gfx.cmd && i < gfx.ngroups; i++)
            SDL_PushGPUDebugGroup(gfx.cmd, gfx.groups[i]);
    }
    return gfx.cmd;
}

/* The command buffer being recorded, to submit, with its debug groups
   closed. */
static SDL_GPUCommandBuffer *gfx_take_cmd(void) {
    SDL_GPUCommandBuffer *cb = gfx.cmd;
    for (int i = 0; cb && i < gfx.ngroups; i++) SDL_PopGPUDebugGroup(cb);
    gfx.cmd = NULL;
    return cb;
}

/* --- debug groups, labels and names ------------------------------------------ */

static const char *gfx_cstr(gs_gfx_bytes s, char *buf, size_t cap);

void gs_gfx_push_group(gs_gfx_bytes name) {
    if (!gfx_need_device("push_group")) return;
    if (gfx.ngroups == GFX_MAX_GROUPS) {
        gfx_misuse("push_group: %d debug groups open already", GFX_MAX_GROUPS);
        return;
    }
    char buf[256];
    char *copy = SDL_strdup(gfx_cstr(name, buf, sizeof buf));
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb || !copy) {
        SDL_free(copy);
        return;
    }
    SDL_PushGPUDebugGroup(cb, copy);
    gfx.groups[gfx.ngroups++] = copy;
}

void gs_gfx_pop_group(void) {
    if (!gfx_need_device("pop_group")) return;
    if (gfx.ngroups == 0) {
        gfx_misuse("pop_group with no debug group open");
        return;
    }
    if ((gfx.pass || gfx.cpass) && gfx.ngroups <= gfx.pass_groups) {
        gfx_misuse("pop_group in a pass, of a group pushed before it: pop it after the pass");
        return;
    }
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (cb) SDL_PopGPUDebugGroup(cb);
    SDL_free(gfx.groups[--gfx.ngroups]);
}

void gfx_end_pass_groups(const char *what) {
    if (gfx.ngroups <= gfx.pass_groups) return;
    gfx_misuse("%s with %d debug group(s) pushed in the pass still open", what,
               gfx.ngroups - gfx.pass_groups);
    while (gfx.ngroups > gfx.pass_groups) {
        if (gfx.cmd) SDL_PopGPUDebugGroup(gfx.cmd);
        SDL_free(gfx.groups[--gfx.ngroups]);
    }
}

void gs_gfx_label(gs_gfx_bytes text) {
    if (!gfx_need_device("label")) return;
    char buf[256];
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (cb) SDL_InsertGPUDebugLabel(cb, gfx_cstr(text, buf, sizeof buf));
}

void gs_gfx_set_texture_name(gs_gfx_texture t, gs_gfx_bytes name) {
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return;
    char buf[256];
    SDL_SetGPUTextureName(gfx.dev, s->tex, gfx_cstr(name, buf, sizeof buf));
}

void gs_gfx_set_texture_cycle(gs_gfx_texture t, uint8_t on) {
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (s) s->no_cycle = !on;
}

void gs_gfx_set_buffer_name(gs_gfx_buffer b, gs_gfx_bytes name) {
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return;
    char buf[256];
    SDL_SetGPUBufferName(gfx.dev, s->buf, gfx_cstr(name, buf, sizeof buf));
}

bool gfx_submit(void) {
    if (!gfx.cmd) return true;
    if (gfx.readbacks_pending) {
        gfx_fence *f = gfx_submit_fenced();
        gfx_fence_drop(f);
        return f != NULL;
    }
    SDL_GPUCommandBuffer *cb = gfx_take_cmd();
    return SDL_SubmitGPUCommandBuffer(cb) || gfx_sdl_fail("submitting commands");
}

static gfx_fence *gfx_attach_fence;

static void gfx_attach(void *item) {
    gfx_readback_slot *r = (gfx_readback_slot *)item;
    if (r->fence || r->failed) return;
    if (gfx_attach_fence) {
        r->fence = gfx_attach_fence;
        gfx_attach_fence->refs++;
    } else {
        r->failed = true;
    }
}

gfx_fence *gfx_submit_fenced(void) {
    /* An empty command buffer's fence still follows all earlier work on the
       queue. */
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    gfx_fence *f = NULL;
    if (cb) {
        gfx_take_cmd();
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
        if (fence) {
            f = (gfx_fence *)calloc(1, sizeof *f);
            f->fence = fence;
            f->refs = 1;
        } else {
            gfx_sdl_fail("submitting commands");
        }
    }
    if (gfx.readbacks_pending) {
        gfx_attach_fence = f;
        gfx_table_each(&gfx.readbacks, gfx_attach);
        gfx_attach_fence = NULL;
        gfx.readbacks_pending = 0;
    }
    return f;
}

bool gfx_fence_done(gfx_fence *f) {
    if (!f->signaled) f->signaled = SDL_QueryGPUFence(gfx.dev, f->fence);
    return f->signaled;
}

bool gfx_fence_wait(gfx_fence *f) {
    if (f->signaled) return true;
    f->signaled = SDL_WaitForGPUFences(gfx.dev, true, &f->fence, 1);
    return f->signaled || gfx_sdl_fail("waiting for the GPU");
}

void gfx_fence_drop(gfx_fence *f) {
    if (!f || --f->refs > 0) return;
    SDL_ReleaseGPUFence(gfx.dev, f->fence);
    free(f);
}

bool gfx_finish(void) {
    gfx_fence *f = gfx_submit_fenced();
    if (!f) return false;
    bool ok = gfx_fence_wait(f);
    gfx_fence_drop(f);
    return ok;
}

void gs_gfx_flush(void) {
    if (!gfx_need_device("flush")) return;
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("flush inside a pass: end the pass first");
        return;
    }
    gfx_finish();
    gfx_poll_timing();
}

/* --- the screen -------------------------------------------------------------- */

/* A 2D texture in a slot the layer owns: the screen's, which keeps its handle
   when a resize replaces the texture. */
static bool gfx_screen_texture(gs_gfx_texture *h, SDL_GPUTextureFormat format, uint32_t usage,
                               int32_t goose_format, int w, int h_) {
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = format;
    ci.usage = usage;
    ci.width = (Uint32)w;
    ci.height = (Uint32)h_;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(gfx.dev, &ci);
    if (!tex) return gfx_sdl_fail("creating the screen");
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, h->id);
    if (s) {
        SDL_ReleaseGPUTexture(gfx.dev, s->tex);
    } else {
        s = (gfx_texture_slot *)gfx_table_add(&gfx.textures, &h->id);
        if (!s) {
            SDL_ReleaseGPUTexture(gfx.dev, tex);
            return false;
        }
    }
    s->tex = tex;
    s->format = format;
    s->usage = usage;
    s->desc.kind = GS_GFX_TEXTURE_2D;
    s->desc.format = goose_format;
    s->desc.usage = (int32_t)usage;
    s->desc.width = w;
    s->desc.height = h_;
    s->desc.depth = 1;
    s->desc.mips = 1;
    s->desc.samples = 1;
    return true;
}

bool gfx_create_screen(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SDL_GPUTextureFormat depth;
    gfx_texture_format(GS_GFX_DEPTH, &depth);
    /* Sampled where it can be, so that it can be read back. */
    uint32_t depth_usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (SDL_GPUTextureSupportsFormat(gfx.dev, depth, SDL_GPU_TEXTURETYPE_2D,
                                     depth_usage | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        depth_usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!gfx_screen_texture(&gfx.screen, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                            GS_GFX_RGBA8, w, h) ||
        !gfx_screen_texture(&gfx.screen_depth, depth, depth_usage, GS_GFX_DEPTH, w, h))
        return false;
    gfx.width = w;
    gfx.height = h;
    /* Black and far until the program draws, rather than whatever the
       memory held. */
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return false;
    SDL_GPUColorTargetInfo ct;
    SDL_zero(ct);
    ct.texture = gfx_texture_slot_of(gfx.screen)->tex;
    ct.clear_color.a = 1.0f;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_zero(dt);
    dt.texture = gfx_texture_slot_of(gfx.screen_depth)->tex;
    dt.clear_depth = 1.0f;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_STORE;
    dt.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    dt.stencil_store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *p = SDL_BeginGPURenderPass(cb, &ct, 1, &dt);
    if (p) SDL_EndGPURenderPass(p);
    return true;
}

gs_gfx_texture gs_gfx_screen(void) {
    gfx_need_device("screen");
    return gfx.screen;
}

gs_gfx_texture gs_gfx_screen_depth(void) {
    gfx_need_device("screen_depth");
    return gfx.screen_depth;
}

void gs_gfx_screen_size(gs_gfx_int2 *out) {
    out->x = gfx.width;
    out->y = gfx.height;
}

/* --- open and close -------------------------------------------------------- */

uint8_t gs_gfx_available(void) { return 1; }

/* A NUL-terminated copy of a Goose string, for SDL. */
static const char *gfx_cstr(gs_gfx_bytes s, char *buf, size_t cap) {
    size_t n = s.len < 0 ? 0 : (size_t)s.len;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(buf, s.data, n);
    buf[n] = 0;
    return buf;
}

/* SDL reports the mouse in window coordinates, which a high-density display
   has fewer of than pixels; gfx reports pixels, as the screen has them. The
   window's size in both, taken again whenever either changes. */
static void gfx_update_input_scale(void) {
    int w = 0, h = 0, pw = 0, ph = 0;
    if (!gfx.window || !SDL_GetWindowSize(gfx.window, &w, &h) ||
        !SDL_GetWindowSizeInPixels(gfx.window, &pw, &ph) || w <= 0 || h <= 0 || pw <= 0 ||
        ph <= 0)
        return;
    gfx.window_w = w;
    gfx.window_h = h;
    gfx.window_pw = pw;
    gfx.window_ph = ph;
}

/* A window coordinate in pixels: multiplied before dividing, in double, so
   one that is a whole number of pixels comes out exactly that. */
static float gfx_to_pixels(float v, int pixels, int size) {
    return (float)((double)v * pixels / size);
}

/* Where the mouse is as the window opens: SDL says only once it moves, or
   once the window gets the keyboard, which one opened behind another does
   not. Wayland keeps the mouse's place on the desktop from programs. Over
   the window or not is left to SDL's mouse focus: the desktop position
   can be stale (an X server under WSLg knows it only from its own
   windows). */
static void gfx_seed_mouse(void) {
    gfx.mouse_in = SDL_GetMouseFocus() == gfx.window;
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver && !SDL_strcmp(driver, "wayland")) return;
    float gx = 0, gy = 0;
    int wx = 0, wy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    if (!SDL_GetWindowPosition(gfx.window, &wx, &wy)) return;
    gfx.mouse_x = gfx_to_pixels(gx - (float)wx, gfx.window_pw, gfx.window_w);
    gfx.mouse_y = gfx_to_pixels(gy - (float)wy, gfx.window_ph, gfx.window_h);
}

/* Without XDG_RUNTIME_DIR, Wayland cannot be reached (unless WAYLAND_DISPLAY
   is a path or WAYLAND_SOCKET hands over a connection), and libwayland says
   so on stderr each time SDL tries it, twice a start. Then SDL is asked for
   the drivers it would try anyway, in its order, without Wayland. The ones
   SDL only uses when asked by name (offscreen, dummy, evdev) stay out, and
   a program or user that named the drivers is left alone. True if it set
   the hint, which the caller resets. */
static bool gfx_skip_unreachable_wayland(void) {
    const char *display = SDL_getenv("WAYLAND_DISPLAY");
    if (SDL_getenv("XDG_RUNTIME_DIR") || SDL_getenv("WAYLAND_SOCKET") ||
        (display && display[0] == '/') || SDL_GetHint(SDL_HINT_VIDEO_DRIVER))
        return false;
    char drivers[256] = "";
    bool wayland = false;
    for (int i = 0; i < SDL_GetNumVideoDrivers(); i++) {
        const char *d = SDL_GetVideoDriver(i);
        if (!strcmp(d, "wayland")) wayland = true;
        else if (strcmp(d, "offscreen") && strcmp(d, "dummy") && strcmp(d, "evdev"))
            SDL_snprintf(drivers + strlen(drivers), sizeof drivers - strlen(drivers), "%s%s",
                         drivers[0] ? "," : "", d);
    }
    if (!wayland || !drivers[0]) return false;
    return SDL_SetHint(SDL_HINT_VIDEO_DRIVER, drivers);
}

/* The embedded shaders' SPIR-V is version 1.3, which Vulkan takes from 1.1
   on, and SDL asks for Vulkan 1.0 unless told otherwise. So ask for 1.3 with
   shaderDemoteToHelperInvocation, which a shader's demote needs, then for
   1.1, then for the device SDL makes by default. Vulkan is SDL's last
   backend, so an attempt no Vulkan device meets fails instead of picking
   another driver. Other backends ignore the Vulkan options. */
static SDL_GPUDevice *gfx_create_device(SDL_GPUShaderFormat formats, bool debug) {
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debug);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,
                           (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN,
                           (formats & SDL_GPU_SHADERFORMAT_DXBC) != 0);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN,
                           (formats & SDL_GPU_SHADERFORMAT_MSL) != 0);
    VkPhysicalDeviceVulkan13Features vk13;
    SDL_zero(vk13);
    vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13.shaderDemoteToHelperInvocation = VK_TRUE;
    SDL_GPUVulkanOptions vulkan[2];
    SDL_zeroa(vulkan);
    vulkan[0].vulkan_api_version = VK_API_VERSION_1_3;
    vulkan[0].feature_list = &vk13;
    vulkan[1].vulkan_api_version = VK_API_VERSION_1_1;
    SDL_GPUDevice *dev = NULL;
    for (int i = 0; i < 2 && !dev; i++) {
        SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vulkan[i]);
        dev = SDL_CreateGPUDeviceWithProperties(props);
    }
    if (!dev) {
        SDL_ClearProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER);
        dev = SDL_CreateGPUDeviceWithProperties(props);
    }
    SDL_DestroyProperties(props);
    return dev;
}

static bool gfx_start(bool windowed, gs_gfx_bytes title, int64_t width, int64_t height,
                      int64_t flags) {
    if (gfx.dev) return gfx_misuse("gfx is already open: close() it first");
    /* A windowed program run by a test runner draws off screen instead. */
    const char *headless = SDL_getenv("GOOSE_GFX_HEADLESS");
    if (headless && SDL_atoi(headless)) windowed = false;
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        return gfx_misuse("a screen of %lld x %lld pixels", (long long)width, (long long)height);
    gfx.error[0] = 0;
    gfx.flags = flags;
    gfx_init_tables();
    SDL_SetAssertionHandler(gfx_assert_handler, NULL);
    SDL_SetHint(SDL_HINT_APP_NAME, "Goose");
    bool hinted = gfx_skip_unreachable_wayland();
    bool video = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (hinted) {
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        /* What SDL says when none of its own drivers starts, not "x11 not
           available". */
        if (!video) SDL_SetError("No available video device");
    }
    if (!video) {
        if (windowed) return gfx_sdl_fail("initializing SDL video");
        /* With no display to open a window on, the offscreen video driver
           still gives SDL_GPU the video device it requires. */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        bool ok = SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        if (!ok) return gfx_sdl_fail("initializing SDL video");
    }
    gfx.video_inited = true;
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    if (gfx_have_dxbc()) formats |= SDL_GPU_SHADERFORMAT_DXBC;
#ifdef NDEBUG
    bool debug = (flags & GS_GFX_DEBUG) != 0;
#else
    bool debug = true;
#endif
    const char *env = SDL_getenv("GOOSE_GFX_DEBUG");
    if (env) debug = SDL_atoi(env) != 0;
    gfx.dev = gfx_create_device(formats, debug);
    if (!gfx.dev) {
        gfx_fail("no GPU device: %s", SDL_GetError());
        gs_gfx_close();
        return false;
    }
    SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(gfx.dev);
    gfx.format = (have & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
               : (have & SDL_GPU_SHADERFORMAT_DXBC) ? SDL_GPU_SHADERFORMAT_DXBC
               : SDL_GPU_SHADERFORMAT_MSL;
    int pw = (int)width, ph = (int)height;
    gfx.window_w = gfx.window_h = gfx.window_pw = gfx.window_ph = 1;
    if (windowed) {
        char buf[512];
        /* The display's every pixel, where some would otherwise draw fewer and
           have the window scaled up. */
        SDL_WindowFlags wf = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (flags & GS_GFX_WINDOW_RESIZABLE) wf |= SDL_WINDOW_RESIZABLE;
        if (flags & GS_GFX_WINDOW_HIDDEN) wf |= SDL_WINDOW_HIDDEN;
        if (flags & GS_GFX_WINDOW_FULLSCREEN) wf |= SDL_WINDOW_FULLSCREEN;
        gfx.window = SDL_CreateWindow(gfx_cstr(title, buf, sizeof buf), (int)width, (int)height,
                                      wf);
        if (!gfx.window || !SDL_ClaimWindowForGPUDevice(gfx.dev, gfx.window)) {
            gfx_sdl_fail("creating the window");
            gs_gfx_close();
            return false;
        }
        if (flags & (GS_GFX_NO_VSYNC | GS_GFX_IMMEDIATE)) {
            /* NO_VSYNC tries mailbox first, which does not tear; IMMEDIATE
               tries immediate first, which does not queue. */
            bool immediate = (flags & GS_GFX_IMMEDIATE) != 0;
            SDL_GPUPresentMode modes[] = {
                immediate ? SDL_GPU_PRESENTMODE_IMMEDIATE : SDL_GPU_PRESENTMODE_MAILBOX,
                immediate ? SDL_GPU_PRESENTMODE_MAILBOX : SDL_GPU_PRESENTMODE_IMMEDIATE };
            for (int i = 0; i < 2; i++) {
                if (SDL_WindowSupportsGPUPresentMode(gfx.dev, gfx.window, modes[i]) &&
                    SDL_SetGPUSwapchainParameters(gfx.dev, gfx.window,
                                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR, modes[i])) {
                    gfx.present_mode = modes[i];
                    break;
                }
            }
        }
        SDL_GetWindowSizeInPixels(gfx.window, &pw, &ph);
        gfx_update_input_scale();
        gfx_seed_mouse();
    }
    /* Headless programs start focused so their ordinary input loop runs. */
    gfx.focused = !gfx.window || (SDL_GetWindowFlags(gfx.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    gfx.cursor_visible = true;
    if (gfx.window) SDL_ShowCursor();
    if (!gfx_create_screen(pw, ph)) {
        char why[sizeof gfx.error];
        memcpy(why, gfx.error, sizeof why);
        gs_gfx_close();
        memcpy(gfx.error, why, sizeof why);
        return false;
    }
    SDL_GPUCommandBufferTimingCapabilities timing;
    SDL_zero(timing);
    gfx.can_time = SDL_GetGPUCommandBufferTimingCapabilities(gfx.dev, &timing) &&
                   timing.supports_timing;
    gfx.gpu_time = -1.0;
    gfx.gpu_time_frame = -1;
    gfx.start_ns = gfx.last_ns = SDL_GetTicksNS();
    gfx.inject_event = SDL_RegisterEvents(1);
    SDL_SetAtomicU32(&gfx_wake_event, SDL_RegisterEvents(1));
    gfx_opens++;
    return true;
}

uint8_t gs_gfx_open(gs_gfx_bytes title, int64_t width, int64_t height, int64_t flags) {
    return gfx_start(true, title, width, height, flags);
}

uint8_t gs_gfx_open_headless(int64_t width, int64_t height, int64_t flags) {
    gs_gfx_bytes none = { NULL, 0 };
    return gfx_start(false, none, width, height, flags);
}

void gs_gfx_close(void) {
    gfx_later_stop();
    if (gfx.dev) {
        if (gfx.pass) SDL_EndGPURenderPass(gfx.pass);
        if (gfx.cpass) SDL_EndGPUComputePass(gfx.cpass);
        gfx.pass = NULL;
        gfx.cpass = NULL;
        gfx_submit();
        SDL_WaitForGPUIdle(gfx.dev);
        for (int i = 0; i < gfx.nin_flight; i++) gfx_fence_drop(gfx.in_flight[i]);
        gfx.nin_flight = 0;
        gfx_release_pipelines();
        gfx_release_resources();
        if (gfx.window) SDL_ReleaseWindowFromGPUDevice(gfx.dev, gfx.window);
        SDL_DestroyGPUDevice(gfx.dev);
    }
    SDL_SetAtomicU32(&gfx_wake_event, 0);
    free(gfx.events);
    for (int i = 0; i < gfx.ninjected; i++) SDL_free(gfx.injected[i]);
    free(gfx.injected);
    SDL_free(gfx.clipboard);
    if (gfx.window) {
        SDL_SetWindowRelativeMouseMode(gfx.window, false);
        SDL_ShowCursor();
        SDL_DestroyWindow(gfx.window);
    }
    if (gfx.video_inited) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    /* What went wrong outlives the device, for gs_gfx_error after a failed
       open. */
    char error[sizeof gfx.error];
    memcpy(error, gfx.error, sizeof error);
    for (int i = 0; i < gfx.ngroups; i++) SDL_free(gfx.groups[i]);
    int64_t misuse = gfx.misuse;
    void *d3dcompiler = gfx.d3dcompiler, *d3dcompile = gfx.d3dcompile;
    memset(&gfx, 0, sizeof gfx);
    memcpy(gfx.error, error, sizeof error);
    gfx.misuse = misuse;
    gfx.d3dcompiler = d3dcompiler;
    gfx.d3dcompile = d3dcompile;
}

int64_t gs_gfx_present_mode(void) { return gfx.window ? (int64_t)gfx.present_mode : -1; }

uint8_t gs_gfx_gpu_memory(gs_gfx_memory_info *out) {
    memset(out, 0, sizeof *out);
    if (!gfx_need_device("memory_info")) return 0;
    SDL_GPUMemoryInfo info;
    SDL_zero(info);
    /* False where the backend can tell nothing, as Metal's can't yet. */
    if (!SDL_GetGPUMemoryInfo(gfx.dev, &info)) return 0;
    out->architecture = (int32_t)info.architecture;
    out->kind = (int32_t)info.kind;
    out->bytes = (int64_t)info.bytes;
    return 1;
}

int64_t gs_gfx_driver(gs_gfx_bytes out) {
    const char *name = gfx.dev ? SDL_GetGPUDeviceDriver(gfx.dev) : "";
    if (!name) name = "";
    int64_t n = (int64_t)strlen(name);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, name, (size_t)k);
    return n;
}

void gs_gfx_set_title(gs_gfx_bytes title) {
    char buf[512];
    if (gfx.window) SDL_SetWindowTitle(gfx.window, gfx_cstr(title, buf, sizeof buf));
}

void gs_gfx_quit(void) { gfx.quit = true; }

/* --- frames ------------------------------------------------------------------ */

/* Shows the screen in the window: a blit into the swapchain texture, which
   may be missing (a minimized window), in which case nothing is shown. */
static void gfx_present(void) {
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return;
    SDL_GPUTexture *swap = NULL;
    Uint32 sw = 0, sh = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cb, gfx.window, &swap, &sw, &sh)) {
        gfx_sdl_fail("acquiring the window's image");
        return;
    }
    if (!swap) return;
    SDL_GPUBlitInfo bi;
    SDL_zero(bi);
    bi.source.texture = gfx_texture_slot_of(gfx.screen)->tex;
    bi.source.w = (Uint32)gfx.width;
    bi.source.h = (Uint32)gfx.height;
    bi.destination.texture = swap;
    bi.destination.w = sw;
    bi.destination.h = sh;
    bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
    bi.filter = sw == (Uint32)gfx.width && sh == (Uint32)gfx.height ? SDL_GPU_FILTER_NEAREST
                                                                     : SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cb, &bi);
}

/* A new event at the end of this frame's list. */
static gs_gfx_event *gfx_event(int32_t kind) {
    if (gfx.nevents == gfx.events_cap) {
        gfx.events_cap = gfx.events_cap ? gfx.events_cap * 2 : 64;
        gfx.events = (gs_gfx_event *)realloc(gfx.events,
                                             sizeof(gs_gfx_event) * (size_t)gfx.events_cap);
    }
    gs_gfx_event *e = &gfx.events[gfx.nevents++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    return e;
}

/* The mouse injected input comes from, whose positions are pixels already,
   and the finger injected touches come from, likewise. */
#define GFX_INJECTED_MOUSE ((SDL_MouseID)-16)
#define GFX_INJECTED_TOUCH ((SDL_MouseID)-17)

/* The modifier keys down now, from the keys' own state, which injected key
   events keep as real ones do. */
static int32_t gfx_mods(void) {
    int32_t m = 0;
    if (gfx.keys[SDL_SCANCODE_LSHIFT] || gfx.keys[SDL_SCANCODE_RSHIFT]) m |= GS_GFX_MOD_SHIFT;
    if (gfx.keys[SDL_SCANCODE_LCTRL] || gfx.keys[SDL_SCANCODE_RCTRL]) m |= GS_GFX_MOD_CTRL;
    if (gfx.keys[SDL_SCANCODE_LALT] || gfx.keys[SDL_SCANCODE_RALT]) m |= GS_GFX_MOD_ALT;
    if (gfx.keys[SDL_SCANCODE_LGUI] || gfx.keys[SDL_SCANCODE_RGUI]) m |= GS_GFX_MOD_GUI;
    return m;
}

/* One text event per character of UTF-8 `text`. */
static void gfx_text_events(const char *text) {
    if (!text) return;
    Uint32 cp;
    while ((cp = SDL_StepUTF8(&text, NULL)) != 0)
        gfx_event(GS_GFX_EVENT_TEXT)->codepoint = cp;
}

/* Real input and the releases on focus loss take the same path, keeping
   held state, per-frame edges, and the ordered events in agreement. */
static void gfx_key_event(SDL_Scancode sc, SDL_Keycode key, bool down, bool repeat) {
    if (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_SCANCODE_COUNT) {
        if (down && !gfx.keys[sc]) gfx.pressed_keys[sc] = 1;
        if (!down && gfx.keys[sc]) gfx.released_keys[sc] = 1;
        gfx.keys[sc] = down;
    }
    gs_gfx_event *ev = gfx_event(GS_GFX_EVENT_KEY);
    ev->scancode = (int32_t)sc;
    ev->keycode = (int32_t)key;
    ev->mods = gfx_mods();
    ev->down = down;
    ev->repeat = repeat;
}

static void gfx_button_event(int button, bool down, int clicks) {
    if (button > 0 && button < 32) {
        uint32_t bit = 1u << button;
        if (down && !(gfx.buttons & bit)) gfx.pressed_buttons |= bit;
        if (!down && (gfx.buttons & bit)) gfx.released_buttons |= bit;
        gfx.buttons = down ? gfx.buttons | bit : gfx.buttons & ~bit;
        if (down) gfx.button_clicks[button] = (uint8_t)clicks;
    }
    gs_gfx_event *ev = gfx_event(GS_GFX_EVENT_MOUSE_BUTTON);
    ev->button = button;
    ev->clicks = clicks;
    ev->down = down;
    ev->x = gfx.mouse_x;
    ev->y = gfx.mouse_y;
}

/* Focus is keyboard focus, not whether the mouse happens to be over us.
   Never recapture on focus gain: the game can wait for a click to resume. */
static void gfx_focus(bool focused) {
    if (gfx.focused == focused) return;
    gfx.focused = focused;
    if (!focused) {
        gs_gfx_set_mouse_relative(false);
        gs_gfx_set_cursor_visible(true);
        for (int sc = 1; sc < SDL_SCANCODE_COUNT; sc++) {
            if (gfx.keys[sc])
                gfx_key_event((SDL_Scancode)sc,
                              SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false),
                              false, false);
        }
        for (int b = 1; b < 32; b++) {
            if (gfx.buttons & (1u << b)) gfx_button_event(b, false, gfx.button_clicks[b]);
        }
        gfx.mouse_dx = gfx.mouse_dy = gfx.wheel = 0;
    } else if (gfx.window) {
        gfx_seed_mouse();
    }
    gfx_event(focused ? GS_GFX_EVENT_FOCUS_GAINED : GS_GFX_EVENT_FOCUS_LOST);
}

/* Ends a frame's commands as Rome's renderer and SDL's swapchain do: at
   most two frame submits on the GPU at once. Done ones are let go; with two
   still running, this waits for the older before submitting, so a program
   drawing faster than the GPU (headless, with nothing else to hold it
   back) waits rather than queueing work and memory without end. */
static void gfx_frame_submit(void) {
    int n = 0;
    for (int i = 0; i < gfx.nin_flight; i++) {
        if (gfx_fence_done(gfx.in_flight[i])) gfx_fence_drop(gfx.in_flight[i]);
        else gfx.in_flight[n++] = gfx.in_flight[i];
    }
    gfx.nin_flight = n;
    if (gfx.nin_flight == 2) {
        gfx_fence_wait(gfx.in_flight[0]);
        gfx_fence_drop(gfx.in_flight[0]);
        gfx.in_flight[0] = gfx.in_flight[1];
        gfx.nin_flight = 1;
    }
    gfx_fence *f = gfx_submit_fenced();
    if (f) gfx.in_flight[gfx.nin_flight++] = f;
    gfx.frames_in_flight = gfx.nin_flight;
}

int64_t gs_gfx_frames_in_flight(void) { return gfx.frames_in_flight; }

#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
static void gfx_nothing(void *unused) { (void)unused; }
#endif

/* The display link paused or running again; nothing where there is none. */
static void gfx_link_pause(bool paused) {
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
    if (gfx.link_paused == paused || !gfx.window) return;
    gfx.link_paused = paused;
    SDL_SetHint(SDL_HINT_IOS_DISPLAY_LINK_PAUSED, paused ? "1" : "0");
#else
    (void)paused;
#endif
}

/* Until an event comes or `seconds` pass. UIKit has no wait of its own in
   SDL, whose fallback polls every millisecond, so there the main run loop
   runs until a source fires (a touch, a block on the main queue such as
   wake() sends) or the time is up. */
static void gfx_wait(double seconds) {
    Uint64 until = SDL_GetTicksNS() + (Uint64)(seconds * 1e9);
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
    gfx_link_pause(true);
    SDL_PumpEvents();
    while (!SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST)) {
        Uint64 now = SDL_GetTicksNS();
        if (now >= until) break;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, (double)(until - now) / 1e9, true);
        SDL_PumpEvents();
    }
    /* a touch, a wake() or the window: the display back at its full rate
       before the frame they may start */
    if (SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST)) gfx_link_pause(false);
#else
    Uint64 now = SDL_GetTicksNS();
    if (now < until) SDL_WaitEventTimeout(NULL, (Sint32)SDL_min((until - now + 999999) / 1000000, 0x7fffffff));
#endif
}

void gs_gfx_wake(void) {
    Uint32 type = SDL_GetAtomicU32(&gfx_wake_event);
    if (!type) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    SDL_PushEvent(&e);
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
    dispatch_async_f(dispatch_get_main_queue(), NULL, gfx_nothing);
#endif
}

/* frame() and frame_held() alike: a held frame shows nothing new (no blit
   into the window, unless the window lost what it showed), submits only
   what was recorded, and with a window waits up to `wait` s for an event. */
static uint8_t gfx_frame_end(const char *name, bool held, double wait) {
    if (!gfx_need_device(name)) return 0;
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("%s() inside a %s pass: end the pass first", name, gfx.pass ? "render" : "compute");
        return 0;
    }
    /* The first call starts the first frame; every later one ends one. */
    if (!held) gfx_link_pause(false);
    if (gfx.window && gfx.frames > 0 && (!held || gfx.must_show)) {
        gfx_present();
        gfx.must_show = false;
    }
    if (!held || gfx.cmd || gfx.readbacks_pending) gfx_frame_submit();
    if (held && gfx.window && wait > 0.0) gfx_wait(wait);
    memset(gfx.pressed_keys, 0, sizeof gfx.pressed_keys);
    memset(gfx.released_keys, 0, sizeof gfx.released_keys);
    gfx.pressed_buttons = gfx.released_buttons = 0;
    gfx.mouse_dx = gfx.mouse_dy = gfx.wheel = 0;
    gfx.nevents = 0;
    bool resized = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                gfx.quit = true;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (!gfx.focused) break;
                gfx_key_event(e.key.scancode, e.key.key, e.key.down, e.key.repeat);
            } break;
            case SDL_EVENT_TEXT_INPUT:
                if (gfx.focused) gfx_text_events(e.text.text);
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (!gfx.focused) break;
                bool injected = e.motion.which == GFX_INJECTED_MOUSE || e.motion.which == GFX_INJECTED_TOUCH;
                gfx.mouse_touch = e.motion.which == SDL_TOUCH_MOUSEID || e.motion.which == GFX_INJECTED_TOUCH;
                int pw = injected ? 1 : gfx.window_pw, w = injected ? 1 : gfx.window_w;
                int ph = injected ? 1 : gfx.window_ph, h = injected ? 1 : gfx.window_h;
                gfx.mouse_x = gfx_to_pixels(e.motion.x, pw, w);
                gfx.mouse_y = gfx_to_pixels(e.motion.y, ph, h);
                if (injected)
                    gfx.mouse_in = gfx.mouse_x >= 0 && gfx.mouse_y >= 0 &&
                                   gfx.mouse_x < (float)gfx.width && gfx.mouse_y < (float)gfx.height;
                gfx.mouse_dx += gfx_to_pixels(e.motion.xrel, pw, w);
                gfx.mouse_dy += gfx_to_pixels(e.motion.yrel, ph, h);
                gs_gfx_event *ev = gfx_event(GS_GFX_EVENT_MOUSE_MOTION);
                ev->x = gfx.mouse_x;
                ev->y = gfx.mouse_y;
            } break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (!gfx.focused) break;
                bool injected = e.button.which == GFX_INJECTED_MOUSE || e.button.which == GFX_INJECTED_TOUCH;
                gfx.mouse_touch = e.button.which == SDL_TOUCH_MOUSEID || e.button.which == GFX_INJECTED_TOUCH;
                gfx.mouse_x = injected ? e.button.x
                                       : gfx_to_pixels(e.button.x, gfx.window_pw, gfx.window_w);
                gfx.mouse_y = injected ? e.button.y
                                       : gfx_to_pixels(e.button.y, gfx.window_ph, gfx.window_h);
                gfx_button_event(e.button.button, e.button.down, e.button.clicks);
            } break;
            case SDL_EVENT_MOUSE_WHEEL: {
                if (!gfx.focused) break;
                gfx.wheel += e.wheel.y;
                gs_gfx_event *ev = gfx_event(GS_GFX_EVENT_MOUSE_WHEEL);
                ev->x = e.wheel.x;
                ev->y = e.wheel.y;
            } break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                gfx.mouse_in = e.type == SDL_EVENT_WINDOW_MOUSE_ENTER;
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (e.window.windowID == (gfx.window ? SDL_GetWindowID(gfx.window) : 0))
                    gfx_focus(e.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                gfx_update_input_scale();
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                gfx_update_input_scale();
                resized = true;
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_RESTORED:
                gfx.must_show = true;
                break;
            default:
                if (gfx.inject_event && e.type == gfx.inject_event && e.user.code >= 0 &&
                    e.user.code < gfx.ninjected) {
                    if (gfx.focused) gfx_text_events(gfx.injected[e.user.code]);
                    SDL_free(gfx.injected[e.user.code]);
                    gfx.injected[e.user.code] = NULL;
                }
                break;
        }
    }
    /* Every injected text has been seen once the queue is empty. */
    gfx.ninjected = 0;
    if (resized && gfx.window) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(gfx.window, &w, &h);
        if (w > 0 && h > 0 && (w != gfx.width || h != gfx.height)) gfx_create_screen(w, h);
    }
    uint64_t now = SDL_GetTicksNS();
    gfx.delta = (double)(now - gfx.last_ns) / 1e9;
    gfx.last_ns = now;
    gfx.frames++;
    if (gfx.time_wanted && gfx.can_time) gfx.timing = true;
    if (gfx.timing) {
        Uint64 id;
        SDL_BeginGPUCommandBufferTimingFrame(gfx.dev, &id);
    }
    gfx_poll_timing();
    return !gfx.quit;
}

uint8_t gs_gfx_frame(void) { return gfx_frame_end("frame", false, 0.0); }

uint8_t gs_gfx_frame_held(double wait) { return gfx_frame_end("frame_held", true, wait); }

double gs_gfx_time(void) {
    return gfx.dev ? (double)(SDL_GetTicksNS() - gfx.start_ns) / 1e9 : 0.0;
}

double gs_gfx_delta_time(void) { return gfx.delta; }

int64_t gs_gfx_frame_count(void) { return gfx.frames; }

int64_t gs_gfx_open_count(void) { return gfx_opens; }

/* --- input ------------------------------------------------------------------- */

/* SDL names a few keys after their platform's labels, and knows each only
   by the name its own platform has: every one of them is taken everywhere.
   Not "Menu", which Windows, X11 and Wayland call the Application key, but
   which is also SDL's name for a key of its own. */
static const struct {
    SDL_Scancode scancode;
    const char *name;
} gfx_key_aliases[] = {
    { SDL_SCANCODE_LALT, "Left Alt" },
    { SDL_SCANCODE_LALT, "Left Option" },
    { SDL_SCANCODE_RALT, "Right Alt" },
    { SDL_SCANCODE_RALT, "Right Option" },
    { SDL_SCANCODE_LGUI, "Left GUI" },
    { SDL_SCANCODE_LGUI, "Left Command" },
    { SDL_SCANCODE_LGUI, "Left Windows" },
    { SDL_SCANCODE_RGUI, "Right GUI" },
    { SDL_SCANCODE_RGUI, "Right Command" },
    { SDL_SCANCODE_RGUI, "Right Windows" },
    { SDL_SCANCODE_APPLICATION, "Application" },
};

static SDL_Scancode gfx_find_scancode(const char *name) {
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    for (size_t i = 0; sc == SDL_SCANCODE_UNKNOWN && i < SDL_arraysize(gfx_key_aliases); i++)
        if (SDL_strcasecmp(name, gfx_key_aliases[i].name) == 0) sc = gfx_key_aliases[i].scancode;
    return sc;
}

static SDL_Scancode gfx_scancode(gs_gfx_bytes name) {
    char buf[64];
    SDL_Scancode sc = gfx_find_scancode(gfx_cstr(name, buf, sizeof buf));
    if (sc == SDL_SCANCODE_UNKNOWN) gfx_misuse("no key is called '%s'", buf);
    return sc;
}

uint8_t gs_gfx_key_down(gs_gfx_bytes name) {
    SDL_Scancode sc = gfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && gfx.keys[sc];
}

uint8_t gs_gfx_key_pressed(gs_gfx_bytes name) {
    SDL_Scancode sc = gfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && gfx.pressed_keys[sc];
}

uint8_t gs_gfx_key_released(gs_gfx_bytes name) {
    SDL_Scancode sc = gfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && gfx.released_keys[sc];
}

static uint32_t gfx_button(int64_t button) {
    if (button < 1 || button > 31) {
        gfx_misuse("no mouse button %lld", (long long)button);
        return 0;
    }
    return 1u << button;
}

uint8_t gs_gfx_mouse_down(int64_t button) { return (gfx.buttons & gfx_button(button)) != 0; }

uint8_t gs_gfx_mouse_pressed(int64_t button) {
    return (gfx.pressed_buttons & gfx_button(button)) != 0;
}

uint8_t gs_gfx_mouse_released(int64_t button) {
    return (gfx.released_buttons & gfx_button(button)) != 0;
}

void gs_gfx_mouse_pos(gs_gfx_float2 *out) {
    out->x = gfx.mouse_x;
    out->y = gfx.mouse_y;
}

void gs_gfx_mouse_delta(gs_gfx_float2 *out) {
    out->x = gfx.mouse_dx;
    out->y = gfx.mouse_dy;
}

float gs_gfx_mouse_wheel(void) { return gfx.wheel; }

uint8_t gs_gfx_mouse_in_window(void) { return gfx.mouse_in; }

uint8_t gs_gfx_focused(void) { return gfx.focused; }

uint8_t gs_gfx_mouse_relative(void) { return gfx.mouse_relative; }

uint8_t gs_gfx_cursor_visible(void) { return gfx.cursor_visible && !gfx.mouse_relative; }

uint8_t gs_gfx_set_mouse_relative(uint8_t on) {
    if (!gfx_need_device("set_mouse_relative")) return 0;
    bool enabled = on != 0;
    if (enabled && !gfx.focused) return gfx_fail("set_mouse_relative: the window is not focused");
    if (gfx.mouse_relative == enabled) return 1;
    if (gfx.window) {
        if (!SDL_SetWindowRelativeMouseMode(gfx.window, enabled))
            return gfx_sdl_fail("set_mouse_relative");
        /* Hide explicitly as well, so SDL's relative-cursor-visible hint
           cannot make the query disagree with what the window displays. */
        if (enabled || !gfx.cursor_visible) SDL_HideCursor();
        else SDL_ShowCursor();
    }
    gfx.mouse_relative = enabled;
    /* A move before the mode change must not turn the camera afterwards.
       SDL flushes real motion too; explicitly include injected/headless motion. */
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    gfx.mouse_dx = gfx.mouse_dy = 0;
    return 1;
}

uint8_t gs_gfx_set_cursor_visible(uint8_t visible) {
    if (!gfx_need_device("set_cursor_visible")) return 0;
    bool show = visible != 0;
    if (!show && !gfx.focused) return gfx_fail("set_cursor_visible: the window is not focused");
    if (gfx.window && !gfx.mouse_relative) {
        if (!(show ? SDL_ShowCursor() : SDL_HideCursor()))
            return gfx_sdl_fail("set_cursor_visible");
    }
    gfx.cursor_visible = show;
    return 1;
}

uint8_t gs_gfx_inject_focus(uint8_t focused) {
    if (!gfx_need_device("inject_focus")) return 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    e.window.timestamp = SDL_GetTicksNS();
    e.window.windowID = gfx.window ? SDL_GetWindowID(gfx.window) : 0;
    return SDL_PushEvent(&e);
}

/* Queues an input event as if it came from the keyboard or mouse, seen at
   the next frame(): what tests drive input with. */
uint8_t gs_gfx_inject_key(gs_gfx_bytes name, uint8_t down) {
    if (!gfx_need_device("inject_key")) return 0;
    SDL_Scancode sc = gfx_scancode(name);
    if (sc == SDL_SCANCODE_UNKNOWN) return 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    e.key.down = down != 0;
    return SDL_PushEvent(&e);
}

static void gfx_inject_pointer(SDL_MouseID which, float x, float y, int64_t button, uint8_t down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.which = which;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - gfx.mouse_x;
    e.motion.yrel = y - gfx.mouse_y;
    SDL_PushEvent(&e);
    if (button) {
        if (!gfx_button(button)) return;
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.timestamp = SDL_GetTicksNS();
        e.button.which = which;
        e.button.button = (Uint8)button;
        e.button.down = down != 0;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    }
}

void gs_gfx_inject_mouse(float x, float y, int64_t button, uint8_t down) {
    if (!gfx_need_device("inject_mouse")) return;
    gfx_inject_pointer(GFX_INJECTED_MOUSE, x, y, button, down);
}

/* A finger going down or up at pixel x, y, as SDL reports a touch: the left
   button, from a mouse that mouse_touch() says is a finger. */
void gs_gfx_inject_touch(float x, float y, uint8_t down) {
    if (!gfx_need_device("inject_touch")) return;
    gfx_inject_pointer(GFX_INJECTED_TOUCH, x, y, GS_GFX_MOUSE_LEFT, down);
}

uint8_t gs_gfx_mouse_touch(void) { return gfx.mouse_touch; }

/* A screen touched directly (a phone's or tablet's), not a trackpad. */
uint8_t gs_gfx_touch_screen(void) {
    int n = 0;
    SDL_TouchID *ids = SDL_GetTouchDevices(&n);
    bool direct = false;
    for (int i = 0; ids && i < n; ++i)
        if (SDL_GetTouchDeviceType(ids[i]) == SDL_TOUCH_DEVICE_DIRECT) direct = true;
    SDL_free(ids);
    return direct;
}

uint8_t gs_gfx_set_fullscreen(uint8_t on) {
    if (!gfx_need_device("set_fullscreen")) return 0;
    if (gfx.window && !SDL_SetWindowFullscreen(gfx.window, on != 0)) return gfx_sdl_fail("set_fullscreen");
    gfx.fullscreen = on != 0;
    return 1;
}

uint8_t gs_gfx_fullscreen(void) {
    if (gfx.window) return (SDL_GetWindowFlags(gfx.window) & SDL_WINDOW_FULLSCREEN) != 0;
    return gfx.fullscreen;
}

int64_t gs_gfx_events(gs_gfx_event_slice out) {
    int64_t k = gfx.nevents < out.len ? gfx.nevents : out.len;
    if (k > 0) memcpy(out.data, gfx.events, sizeof(gs_gfx_event) * (size_t)k);
    return gfx.nevents;
}

void gs_gfx_text_input(uint8_t on) {
    if (!gfx_need_device("text_input") || !gfx.window) return;
    if (on) SDL_StartTextInput(gfx.window);
    else SDL_StopTextInput(gfx.window);
}

/* Text as if typed, seen at the next frame(): a user event in the queue
   keeps its place among injected keys and clicks. */
uint8_t gs_gfx_inject_text(gs_gfx_bytes text) {
    if (!gfx_need_device("inject_text")) return 0;
    if (!gfx.inject_event) return gfx_fail("inject_text: SDL has no user event left");
    if (gfx.ninjected == gfx.injected_cap) {
        gfx.injected_cap = gfx.injected_cap ? gfx.injected_cap * 2 : 8;
        gfx.injected = (char **)realloc(gfx.injected, sizeof(char *) * (size_t)gfx.injected_cap);
    }
    size_t n = text.len > 0 ? (size_t)text.len : 0;
    char *copy = (char *)SDL_malloc(n + 1);
    if (n) memcpy(copy, text.data, n);
    copy[n] = 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = gfx.inject_event;
    e.user.timestamp = SDL_GetTicksNS();
    e.user.code = gfx.ninjected;
    gfx.injected[gfx.ninjected++] = copy;
    return SDL_PushEvent(&e);
}

int64_t gs_gfx_scancode(gs_gfx_bytes name) {
    char buf[64];
    return gfx_find_scancode(gfx_cstr(name, buf, sizeof buf));
}

int64_t gs_gfx_key_name(int64_t scancode, gs_gfx_bytes out) {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) {
        gfx_misuse("key_name: no scancode %lld", (long long)scancode);
        return 0;
    }
    const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);
    int64_t n = (int64_t)strlen(name);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, name, (size_t)k);
    return n;
}

/* Headless, gfx keeps a clipboard of its own, which keeps tests and the
   programs a test runner drives off the system's. */
int64_t gs_gfx_clipboard(gs_gfx_bytes out) {
    if (!gfx_need_device("clipboard")) return 0;
    char *text = gfx.window ? SDL_GetClipboardText() : gfx.clipboard;
    int64_t n = text ? (int64_t)strlen(text) : 0;
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, text, (size_t)k);
    if (gfx.window) SDL_free(text);
    return n;
}

uint8_t gs_gfx_set_clipboard(gs_gfx_bytes text) {
    if (!gfx_need_device("set_clipboard")) return 0;
    size_t n = text.len > 0 ? (size_t)text.len : 0;
    char *copy = (char *)SDL_malloc(n + 1);
    if (!copy) return gfx_fail("out of memory for the clipboard");
    if (n) memcpy(copy, text.data, n);
    copy[n] = 0;
    if (!gfx.window) {
        SDL_free(gfx.clipboard);
        gfx.clipboard = copy;
        return 1;
    }
    bool ok = SDL_SetClipboardText(copy);
    SDL_free(copy);
    return ok || gfx_sdl_fail("set_clipboard");
}
