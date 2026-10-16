/* SDL_GPU leaks a command buffer whose fence is released after
   SDL_QueryGPUFence says it is done but before SDL itself has seen it
   signaled: the fence goes back to the pool, the next submit takes it and
   resets it before its cleanup pass looks, and the earlier command buffer,
   with every texture and uniform buffer it used, is never cleaned up.

   Each frame clears a cycled depth texture 20 times in one command buffer,
   so each leaked command buffer holds 20 textures. Run with "query" (query,
   then release) or "wait" (SDL_WaitForGPUFences, then release); only
   "query" grows. Prints resident memory growth over frames 20 to 120. */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static long resident_mb(void) {
    long size = 0, resident = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1;
    if (fscanf(f, "%ld %ld", &size, &resident) != 2) resident = -1;
    fclose(f);
    return resident < 0 ? -1 : resident * 4096 / (1024 * 1024);
}

int main(int argc, char **argv) {
    bool wait = argc > 1 && !strcmp(argv[1], "wait");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
    if (!dev) {
        fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = 256, .height = 256, .layer_count_or_depth = 1, .num_levels = 1,
    };
    SDL_GPUTexture *depth = SDL_CreateGPUTexture(dev, &info);
    long before = 0;
    for (int frame = 0; frame < 120; frame++) {
        if (frame == 20) before = resident_mb();
        SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(dev);
        for (int i = 0; i < 20; i++) {
            SDL_GPUDepthStencilTargetInfo d = {
                .texture = depth, .clear_depth = 1.0f, .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE, .cycle = true,
            };
            SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cb, NULL, 0, &d));
        }
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
        if (wait) {
            SDL_WaitForGPUFences(dev, true, &fence, 1);
        } else {
            while (!SDL_QueryGPUFence(dev, fence)) SDL_Delay(1);
        }
        SDL_ReleaseGPUFence(dev, fence);
    }
    printf("%s: resident memory grew %ld MB over frames 20 to 120\n",
           wait ? "wait" : "query", resident_mb() - before);
    SDL_ReleaseGPUTexture(dev, depth);
    SDL_DestroyGPUDevice(dev);
    SDL_Quit();
    return 0;
}
