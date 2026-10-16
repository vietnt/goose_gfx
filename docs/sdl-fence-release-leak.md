# SDL_GPU leaks a command buffer whose fence is released after a query

Found while pacing frames (`frame()` keeps at most two on the GPU). Seen
with the SDL in `third_party/SDL` (`fa2c02b`) on Vulkan, Mesa Lavapipe. The
code involved is unchanged on SDL `main` as of `6defbc012` (2026-10-14).
Not reported to SDL yet. Not tried on Direct3D 12 or Metal.

## What happens

A program submits with `SDL_SubmitGPUCommandBufferAndAcquireFence`, polls
the fence with `SDL_QueryGPUFence` until it is signaled, then calls
`SDL_ReleaseGPUFence`. Doing that every frame, the command buffers those
fences belonged to are never cleaned up: the uniform buffers they used are
never returned, and the textures they used keep a reference, so cycling one
of them always makes a new texture.
Memory grows every frame until the device is destroyed.

Calling `SDL_WaitForGPUFences` on the fence before releasing it, which
returns at once when it is signaled, avoids it.

## Why

In `SDL_gpu_vulkan.c`, a command buffer is cleaned up
(`VULKAN_INTERNAL_CleanCommandBuffer`) only when SDL itself sees its fence
signaled: in the cleanup pass at the end of `VULKAN_Submit`, or in
`VULKAN_WaitForFences`. `VULKAN_QueryFence` only reads the fence status.

`VULKAN_ReleaseFence` drops the application's reference and returns the
fence to the pool, but the submitted command buffer still points at it
(`inFlightFence`). The next `VULKAN_Submit` takes that fence from the pool
(`VULKAN_INTERNAL_AcquireFenceFromPool`, which resets it) before its
cleanup pass runs. The pass then finds the earlier command buffer's fence
unsignaled and skips it. A program that queries and releases each frame's
fence gets the same fence back from the pool on every submit, so this
repeats every frame and no frame's command buffer is ever cleaned up.

## Reproducing it

[`test/sdl/fence_release_leak.c`](../test/sdl/fence_release_leak.c) uses
SDL alone. Each frame clears a cycled 256 x 256 depth texture 20 times in
one command buffer, submits it with a fence, and releases the fence after
querying it, or after waiting on it with `wait`:

```sh
cc -o fence_release_leak test/sdl/fence_release_leak.c -Ithird_party/SDL/include \
    <build dir>/libSDL3.a -lm -ldl -lpthread
./fence_release_leak query
./fence_release_leak wait
```

On the orb (Lavapipe, `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`):

```
query: resident memory grew 509 MB over frames 20 to 120
wait: resident memory grew 0 MB over frames 20 to 120
```

## What goose_gfx does

`gfx_fence_drop` waits on a fence before releasing it (`src/gfx/gfx_device.c`).
`gfx_pacing` checks that 100 frames of 20 depth clears stay within 16 MB.
A fix in SDL would let the wait go: for example, `VULKAN_ReleaseFence`
could leave the fence to the command buffer when it has not been cleaned
up yet, or the cleanup pass could run before a submit takes a fence.
