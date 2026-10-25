# The graphics module (`gfx`)

This document describes the optional graphics module's implementation, test
coverage, and remaining work. See Goose's `docs/stdlib.md` for the API reference and
`gfx.goose` for the module source.

For **text rendering, fonts, labels and game HUDs**, use the existing
[`ui` module](../stdlib.md#text-rendering-and-game-huds): `ui::label` places
text through layout, `ui::draw_text` places it on a window's canvas, and
`ui::render` draws it over a gfx scene. Ordinary game text does not need a
new font atlas or text renderer built directly on gfx. Custom sprite batching
and games using their own bitmap artwork are separate concerns.

## What it is

`import gfx;` gives a Goose program a window, input, GPU buffers and textures,
graphics and compute pipelines, render and compute passes, and reading results
back. Underneath is SDL3's GPU API (SDL_GPU), which draws through Direct3D 12,
Vulkan or Metal. Shaders are GLSL, compiled into the program by the builtin
`embed_shader` when the program is compiled: written in it as a `"""` string
after their stage (`embed_shader("frag", """ ... """)`), or in a file
(`embed_shader("file.frag")`).

    Goose program --extern fn--> gs_gfx_* (goose_gfx, native C) --> SDL3 (static)

The layer between Goose and SDL, `goose_gfx` (`src/gfx/`), is a natively
compiled static library rather than a runtime header spliced into the
generated C:

* Neither the generated C nor TinyCC ever sees an SDL header. A JIT run only
  needs the layer's own functions defined for it with `tcc_add_symbol`: the
  64 of `src/gfx/gfx_api.h`, not SDL's 1,270.
* Code that needs platform headers, such as calling `D3DCompile`, stays in
  native code.
* JIT and AOT execute the same compiled layer: a JIT run gets the copy linked
  into `goose`, an AOT program links the same archive.

## Pinning this fork

`vietnt/goose_gfx` is a fork; a program reaches it by declaring the package
in its root file at a commit, which wins over the blessed `gfx` the compiler
knows:

```goose
import gfx from "github.com/vietnt/goose_gfx@<full 40-character commit hash>";
```

Rome_goose does this in its `src/main.goose`. The hash must be the full one:
Goose fetches the pinned ref shallowly, and a shallow fetch cannot resolve an
abbreviated hash. A commit, once fetched, is never fetched again; moving the
pin means editing that line. The shader built-ins added by the compiler fork
(`vietnt/goose`) come with the compiler, not this package, so a program that
uses them also needs a `goose` built from that fork.

## How it is built

* **A package** (Goose's `docs/design/packages.md`): this repository holds the
  modules `gfx.goose`, `audio.goose` and `ui.goose`, the sidecar `gfx.build`
  naming the CMake target `goose_gfx_package` and `main_thread_only`, the
  three layers under `src/`, and the CMakeLists that builds them with SDL3
  and Nuklear. The three are one package because they share SDL and ui
  draws through gfx; `import gfx;`, `import audio;` and `import ui;` find
  it through the blessed table (`src/blessed.txt`). A compiler configured
  with `-DGOOSE_BUILTIN_LIBS=all` has it compiled in (`cmake/package.cmake`):
  the layers' functions reach a JIT run through the symbol table CMake
  generates from the modules' `extern fn`s, and the link closure of a probe
  executable, read from CMake's file API reply once the compiler is built
  (`cmake/linkclosure.cmake`), is the response file
  `build/pkg/gfx/<config>/link-{cc,msvc}.rsp` that `goose build` links
  programs with (`goose --gfx-link msvc|cc` prints its path). A compiler
  without it builds the package into `GOOSE_HOME` the first time a program
  uses it, through the wrapper project of `src/native.h`, and loads the
  shared library that build makes for JIT runs.
* **SDL3** comes from the submodule `third_party/SDL` (shallow), on the
  `rome-3.4` branch of [vietnt/SDL](https://github.com/vietnt/SDL): upstream
  `release-3.4.18` plus the commits Rome needs (see that branch's
  `docs/README-rome-fork.md`, which also says why the branch is never
  rewritten). Two of them fix SDL bugs this package used to work around:
  * SDL's Vulkan backend compacts fragmented memory during a submit, moving
    resources to new memory with a copy of their contents. It skipped
    resources used by pending copies but not ones another command buffer
    still being recorded had written otherwise, so their writes went to the
    old memory and everything after sampled and read back the copy taken
    before them (`test/gfx/gfx_compute_after_release`). It now compacts only
    when no other command buffer is being recorded.
  * A fence released after a query alone left its command buffer never
    cleaned up on Vulkan and Direct3D 12 (`docs/sdl-fence-release-leak.md`).
    Both now give `SubmitAndAcquireFence`'s caller a reference of its own, as
    Metal already did.
  The package's CMakeLists builds SDL static, without its 2D renderer,
  OpenGL or the camera, none of which SDL_GPU needs. `SDL_DEPS_SHARED` stays
  on, so on Linux SDL links only the C library and loads X11, Wayland, audio
  and udev at run time.
* **The static C runtime on Windows** (`CMAKE_MSVC_RUNTIME_LIBRARY
  MultiThreaded`, in every configuration): the compiler, TinyCC, SDL and the
  layer share the runtime a plain `cl game.c` links, so a program can link the
  same archives. `goose.exe` imports KERNEL32 and ADVAPI32 directly; the nine
  system DLLs SDL adds are delay-loaded, so a compiler run that opens no window
  does not load them. Compile times were unchanged.
* **`cute_spirv`** (`third_party/cute_spirv`, copied from cute_framework, see
  its README) compiles GLSL to SPIR-V and transpiles it to HLSL and MSL. It is
  built into every compiler, SDL or not, as `goose_shaderc` (`src/shaderc.c`),
  since shader compilation is the compiler's side of graphics. The blob it
  writes is a contract between the compiler (`src/gfx_blob.h`) and the
  package's layer, which carries its own copy of the header
  (`src/gfx/gfx_blob.h`) and checks the blob's version.

**Building without graphics**: a `GOOSE_BUILTIN_LIBS` without gfx, a
checkout without the SDL or Nuklear submodule, or a Linux machine without
the X11 and Wayland development files (where SDL's own configure would
silently build an SDL that cannot open a window, the package's CMakeLists
says what to install and builds nothing). The package is then not compiled
in, and the compiler's configure says so. Gfx programs typecheck and
generate the same C either way, and shaders still compile; running or
building one then needs cmake and a C compiler, and the submodules.

## The compiler's side

* **`embed_shader`** is a builtin whose arguments are string literals, or
  `let` or `const` globals initialized with one, which the checker puts in
  their place. `embed_shader("frag", source, ...)` (`"vert"`, `"frag"` or
  `"comp"`) compiles GLSL written in the program, its parts joined as lines,
  with `#include` relative to the file containing the call;
  `embed_shader("x.vert")` (`.vert`, `.frag` or `.comp`) compiles a file,
  resolved relative to that file, with `#include` relative to the shader.
  The checker compiles each distinct shader once (`TypeCheck::EmbedShader`,
  `src/gfx.h`, `src/shaderc.c`), keeping the blobs in `Ast::shaders` and a
  pointer to its blob on the call for codegen. Compilation failures are reported
  at the call unless the shader compiler identifies a source line. For GLSL
  written in the program, that line maps directly to the corresponding line
  of the `"""` string (`StrLit::multiline`). If the source comes from a
  global, the message also identifies the call that compiled it. Errors in
  shader files include the shader's own `file:line`. The result is a `const u8[:]` into static
  data, like a string literal's, which codegen emits as an initializer list
  of bytes.
* **The blob** (`src/gfx_blob.h`, and the package's copy of it, which `shaderc.c` and the layer share)
  holds SPIR-V, MSL and HLSL together, so the generated C builds anywhere,
  plus the reflection SDL_GPU needs: counts of samplers, storage textures and
  storage buffers (read-only and read-write), the std140 size of each uniform
  block, the vertex inputs by location, and a compute shader's local size.
* **SDL_GPU's binding contract is checked at compile time.** Each resource must
  sit in the set SDL_GPU gives its kind and stage, numbered from 0 in the order
  samplers, storage textures, storage buffers. A resource in the wrong place
  works on no backend or on only some; here it is a compile error naming the
  rule (`test/gfx/gfx_err_shader_binding.goose`).
* **Calls into the layer** are extern fns of the gfx package: codegen
  notes the packages a program's extern calls reach (`NativePackagesUsed`),
  and a JIT run takes their functions from the compiled-in table or the
  package's library before relocating the program.
* **A `thread_fn` never reaches the layer**: SDL_GPU and windowing are
  main-thread only, and a thread program calling into gfx is a compile error.

## The layer (`src/gfx/`)

The layer handles SDL_GPU's resource management and validation rules for the
Goose API. `gfx_api.h` lists every function once (`GS_GFX_API`) and every constant
(`GS_GFX_CONSTANTS`); `test/api_check.py` checks `gfx.goose`
declares exactly those functions with the same C shapes, the same struct
fields in the same order, and the same constant values.

* **Handles** are one `u32`: a slot index and a generation, so a released
  handle is an error, not a crash.
* **Errors come in two kinds.** A failure outside the program's control (no
  GPU, a missing file) returns false or a zero handle, with the reason in
  `gfx::error()`. A call the program should not have made is a *misuse*: it is
  skipped, printed as it happens, counted, and the Goose side aborts with the
  reason at the next `frame()`, `check()`, read back or `close()`. SDL_GPU's
  own validation asserts are routed into the same path instead of SDL's
  dialog. Draws and dispatches check that every sampler, storage resource,
  uniform block and vertex buffer the bound pipeline's shaders read was
  provided, and a pushed uniform block must have the shader's std140 size.
* **The screen** is an RGBA8 texture with a depth texture, not the swapchain:
  a frame draws into it, and `frame()` blits it to the window. Pipelines then
  never depend on the swapchain's format, screenshots and readback of the
  screen are ordinary texture downloads, and a minimized window (no swapchain
  image) only skips the blit. A headless device (`open_headless`, or `open`
  under `GOOSE_GFX_HEADLESS=1`) has the same screen and no window.
* **A still screen costs nothing.** `frame_held(wait)` ends a frame that drew
  nothing new: no blit into the window and no present (the window keeps the
  picture it shows, so on Metal, Vulkan and Direct3D 12 alike nothing is
  acquired from the swapchain), a submit only when commands were recorded,
  and with a window a wait of up to `wait` s for an event before the
  frame's events are handled. An exposed, shown or restored window is shown
  again by the next held frame, from the screen, which held frames leave as
  the last drawn frame left it. `wake()`, from any thread, ends the wait (a
  download's progress, an answer from the system). SDL has no wait of its
  own on UIKit (its fallback polls every millisecond), so there the wait
  runs the main run loop until a source fires; elsewhere it is
  `SDL_WaitEventTimeout`. On a display faster than 60 Hz SDL's UIKit window
  keeps a display link that asks for the full rate and wakes the main thread
  at every refresh; a held frame's wait pauses it (our SDL fork's
  `SDL_HINT_IOS_DISPLAY_LINK_PAUSED`), and an event ending the wait or the
  next shown frame runs it again, so moving pages keep 120 Hz and a still
  one lets the display slow down. Headless it does not wait (`gfx_held`;
  `test/window/gfx_held.goose` checks the wait and wake()).
* **At most two frames are on the GPU.** `frame()` submits each frame with
  a fence and, with two earlier ones still running, waits for the older
  before submitting, headless as well as windowed; `frames_in_flight()`
  says how many were running at the last submit. A done fence is released
  after a query alone; `gfx_pacing` checks that this keeps memory flat
  (`docs/sdl-fence-release-leak.md` for the SDL bug that once made it leak).
  Overlapping frames hold more memory, flat rather than growing: a target
  cleared every frame is cycled, so SDL keeps a copy for each frame the GPU
  may still be using, up to three. For the screen's colour and depth at
  1920 x 1080 that is four more 8 MiB textures, which SDL puts in one more
  64 MiB memory block. Rome_goose's G8 scene holds about 67 MiB more paced
  than flushing every frame, and logging SDL's allocations in that scene
  shows this block and these four textures as the whole difference. Rome's renderer cycles its cleared targets the
  same way. Clearing without cycling would give the memory back at the cost
  of the overlap (each frame's clear waiting for the last frame's reads of
  that target), so it is on by default, and `set_cycle(t, false)` turns it
  off for one target, the screen included, where the memory counts for more
  (`gfx_no_cycle`). A compute pass cycles what it writes
  only when asked (`begin_compute(buffers, textures, true)`), since what it
  leaves unwritten is then undefined: for a pass that rewrites the whole of
  each, as Rome's wind fields do and its macro weights do not
  (`gfx_compute_cycle`).
* **Everything is in pixels.** A window always asks for the display's full
  pixel density, so on a high-density display its screen has more pixels
  than its size in the display's coordinates, which SDL gives the mouse in:
  positions and motion are multiplied by the window's size in pixels and
  divided by its size in those coordinates, in double, so a whole pixel
  comes out whole. Injected mouse input is in pixels already.
* **First-person input** uses SDL's per-window relative mouse mode. Turning
  it on hides the cursor and gives continuous pixel deltas. Focus changes
  are ordered events as well as a query. Focus loss clears held input and
  motion, releases relative mode, and shows the cursor; focus gain never
  recaptures without a call from the program. The headless implementation
  keeps the same logical states, with injected focus for deterministic tests,
  without changing the desktop cursor.
* **Pipelines are created for their render targets.** A pipeline description
  does not name its targets. The layer creates a pipeline on first use with
  each combination of color formats, depth format, and sample count.
* **The vertex layout comes from the shader**: its inputs in location order,
  packed one after another as a Goose struct lays out its fields, per vertex
  from buffer 0, from `instance_location` on per instance from buffer 1.
  `PipelineDesc.formats` overrides a location's format, for bytes as colors.
  SDL's own vertex buffer slots are numbered with no gap, so a pipeline with
  only per-instance inputs reads buffer 1 from SDL's slot 0; vertex buffers
  are therefore bound into SDL at the draw, into the slots its pipeline
  reads them from (`gfx_instance_only`).
* **Copies are in program order.** A new resource's first contents go through
  a command buffer of their own; any other upload, readback or mip generation
  is recorded in the frame's command buffer, and so happens between passes. A
  rewrite of a whole buffer lets SDL cycle it rather than wait for a draw still
  using it. A readback is
  recorded the same way, into a transfer buffer of its own, and is pending
  until that command buffer is submitted; the submit's fence is shared by
  every readback it carried, freed when the last lets go. `ready` polls
  it, `take` waits on it (submitting first, if the readback is still
  pending), copies the bytes out and frees the readback, and a blocking
  `read_texture` or `read_buffer` is a readback taken at once.
* **Depth reads back as f32.** SDL_GPU cannot download a depth texture on
  every backend (Direct3D 12 copies depth only as a whole subresource), so
  `read_texture` on one draws the region into an R32F target, a full-screen
  triangle that `texelFetch`es it, and reads that: one f32 a texel for every
  depth format, D16 and D24 normalized, stencil left out. The region's
  origin reaches the shader from a vertex buffer, so the program's pushed
  uniforms are untouched. Depth textures, the screen's
  included, are created sampled where the device can sample their format;
  where it cannot, the read fails with the reason in `error()`. Any mip of
  a 2D texture, an array layer or a cube face reads back, a cube face
  sampled at its texels' centres; multisampled depth is a misuse (3D
  textures cannot be depth targets in SDL). A pass draws into a depth
  target's mip and layer from `PassDesc.depth_mip` and `depth_layer`.
* **Multisampled color textures** read back resolved, their samples
  averaged as a pass's resolve target gets them: the read records a render
  pass that loads the texture and resolves it into a texture of its own,
  keeping the samples, and downloads that. Integer formats cannot be
  resolved, so reading them is a misuse, as reading multisampled depth is.
* **Block-compressed textures** (BC1, BC3, BC4, BC5, BC7) are SAMPLED only,
  with a width and height that are multiples of 4, as Direct3D 12 requires.
  Their regions are whole 4 x 4 blocks, but where a mip's right or bottom
  edge cuts a block short, and `region_size` counts their bytes, as it does
  every format's (`SDL_CalculateGPUTextureFormatSize`). A transfer's row
  length and rows are rounded up to whole blocks, which Vulkan requires.
  Their mips are uploaded, not generated. Reading one back on Direct3D 12 is
  refused: SDL 3.4's Direct3D 12 download of a compressed texture copies
  the region's texel rows, not its block rows, out of its staging buffer
  (`D3D12_DownloadFromTexture` records the texel height, and the copy back
  loops over it), past the end of a tightly sized one. Uploading and
  sampling work there; the refusal goes once SDL copies block rows.
* **Shaders**: the device takes SPIR-V where it can, else DXBC, else MSL. DXBC
  is the blob's HLSL compiled at load time by `d3dcompiler_47.dll`, which ships
  with Windows 10 and 11, loaded by name. Each SDL shader is made once per blob.
* **Debug mode** (SDL's validation, and the platform's validation layers where
  installed) is on in a debug build of the layer, with the `DEBUG` open flag,
  or `GOOSE_GFX_DEBUG=1`.

## The Goose module (`gfx.goose`)

Namespace `gfx`: constants, the handle and description structs, the `extern`
declarations, and thin wrappers. Descriptions have defaults and are passed by
value to the wrappers, which pass them on by reference, so
`gfx::pipeline(vs, fs, gfx::PipelineDesc { depth_test: true })` works.
`uniforms(stage, slot, value)` pushes any flat struct; readback helpers infer
the element type from the array they append to (`read_buffer(b, floats, n)`),
and `read_pixels(t)` gives a texture's bytes. Column-major `mat4` helpers
(`perspective`, `ortho`, `look_at`, `rotate`, `mul`) produce matrices for
SDL_GPU's clip space. Goose's `docs/stdlib.md` is the reference.

## Testing

`test/gfx/gfx_mouse_capture.goose` checks capture/visibility transitions,
focus loss and reacquisition, release edges/events, ignored background input,
and headless close/reopen. `python test/window/run_input_test.py --exe
<goose>` complements it on a machine with a display and GPU: a hidden real
window verifies the SDL mode/visibility setters, unrelated-window focus
filtering, double-click cleanup, and cursor restoration after close. Physical
mouse confinement and Alt-Tab still need an interactive check on each platform.

* **`test/gfx/`** in the regular suite: headless programs that render into
  textures and read them back (geometry, depth, instancing, storage-buffer
  vertex pulling, blending, MSAA resolve; 2D, 3D, cube and array textures,
  regions, mips, float texels, rendering into a cube face), compute (storage
  buffers, a storage texture, atomics, a compute shader sampling a texture),
  frames and injected input, input events in order with key names and the
  headless clipboard, and one runtime misuse aborting as designed. They
  draw only on pixel boundaries, so their output is exact on every backend.
  They build and run AOT and JIT where the compiler has the layer, and a
  machine without a GPU device reports them skipped (the program prints `gfx:
  no GPU device`). Their shaders are written in them. Beside them are a
  check that a shader from a file, written in the program and given in parts
  compiles to the same blob, the rejection tests (a shader that does not
  compile, whose error must land on the offending line of the program, also
  in a part a global holds; a binding in the wrong set; a missing file; an
  unknown extension or stage name; source given without a stage; a local
  variable as an argument; gfx from a `thread_fn`), probes of the hidden
  `--compile-shader` flag on the shader files kept for it, and the API check.
  The suite sets `GOOSE_GFX_HEADLESS=1`, so nothing it runs opens a window.
* **`samples/27_gfx_cube.goose`** runs in the samples runner, headless, for 30
  frames.
* **`test/window/`** is not in the suite, since it needs a display: a
  showcase program using a wide spread of the API in one scene (a shadow map,
  a multisampled HDR pass, instances and particles moved by compute shaders, a
  cube-map sky, 2D, 3D and array textures, an sRGB PNG round trip, wireframe,
  lines, blending, a viewport and scissor, tone mapping), checking itself
  through readbacks and saving screenshots. `run_window_test.py` runs it in
  every way the machine can and compares the screenshots across them.
  `gfx_present.goose` prints the present mode each open() flag gives a
  window (`goose run test/window/gfx_present.goose`; on Linux with no
  display, under `xvfb-run` with `SDL_VIDEODRIVER=x11`). Lavapipe under
  Xvfb gives 0, 2, 1 for no flag, NO_VSYNC and IMMEDIATE. Metal has no
  mailbox, so NO_VSYNC gives immediate there: 0, 1, 1.

### Platforms tested

| | Windows 11 (RTX 3080) | Linux (Ubuntu 24.04 under WSL2, WSLg) | macOS |
|---|---|---|---|
| Build, opt-out builds | yes | yes | not run |
| Suite, incl. `test/gfx/` | yes: MSVC, TinyCC | yes: gcc, TinyCC; sanitizer profile without SDL | not run |
| Showcase | JIT, cl, clang-cl, clang; Direct3D 12 and Vulkan | JIT, gcc, clang; Vulkan | not run |

On Windows the showcase's screenshots from Direct3D 12 and Vulkan differ by
0.65 of 255 per channel on average. On Linux with no display at all (the CI
runner's case, simulated by unsetting `DISPLAY` and `WAYLAND_DISPLAY`), SDL's
video initialization fails and `open_headless` falls back to SDL's offscreen
video driver, which gives the Vulkan backend what it needs: the `test/gfx/`
programs run there unchanged. Nothing on macOS has run: the MSL that
cute_spirv emits, SDL's Metal backend under this layer, the framework list
in `link-cc.rsp`, and TinyCC's arm64 calls into the layer are all unverified
until CI or a Mac runs them.

## Decisions on the plan's open questions

* The builtin is `embed_shader`, one shader per stage: its GLSL written in
  the program after the stage's name, or a file whose extension names it.
* gfx from a `thread_fn` is a compile error.
* Uniform structs are checked against the shader's block size, not member
  offsets.
* AOT programs stay console applications.
* The cost of static SDL on macOS compiler start-up, and whether `goose` should
  split off a `goose-gfx`, is still to be measured on a Mac.

## Follow-up work

* **macOS**: run the suite and the showcase; measure the start-up cost above.
* **Compiler bugs found on the way**, each with a workaround in the module or
  the tests: a fresh resizable result passed straight to an inlined
  function's slice parameter produces invalid C at `-O1`/`-O2` (bind it to a
  local first); and a struct field's default cannot name a global constant
  (the gfx descriptions spell their defaults as numbers).
* **API**: gamepads and audio (SDL has them all), checking uniform block
  member offsets rather than size alone, and readback forms that return a
  fresh array of an explicit element type (`read_buffer<f32>(b, n)`) beside
  the ones that append to an out-array.
* **Blocked outside goose_gfx.** Compressed-texture downloads on Direct3D
  12 (above) need SDL itself to change. GPU time (`gpu_time()`), GPU memory
  (`memory_info()`) and copies from a buffer into a texture on the GPU
  (`copy_buffer_to_texture()`) needed it too; they use the API that
  vietnt/SDL `rome-3.4` adds.
* **Shader built-ins from the compiler fork.** `gl_FragDepth`,
  `gl_FrontFacing`, `gl_SampleID`, `textureGather`, `textureQueryLod`,
  `texture(s, P, bias)` and `demote` are in `embed_shader`'s GLSL front end
  in `vietnt/goose`, not in upstream's compiler. This layer's part is
  `demote`, which Vulkan runs only with the device feature
  `shaderDemoteToHelperInvocation`: the device is made with Vulkan 1.3 and
  that feature where the machine has them, else with 1.1, else as SDL makes
  it by default. `test/gfx/gfx_demote.goose` says `// needs: demote` on its
  first line, and the runner skips it with a compiler that cannot compile
  that.
* **A storage buffer's `.length()` on Metal.** It reads 0 there: SDL_GPU's
  Metal backend gives a shader no buffer sizes, and the compiler's MSL
  prints the length of a runtime array as 0 (Vulkan and Direct3D 12 query
  it). The fix is in both: the compiler's MSL declares a hidden uniform
  block of byte sizes and divides by the stride, and this layer pushes the
  size of each storage buffer it binds into it, as SPIRV-Cross's buffer-size
  buffer does.
* **Clear values at texture creation.** Direct3D 12's debug layer warns
  (#820, #821 `CLEAR*VIEW_MISMATCHINGCLEARVALUE`) each time a target is
  cleared to a value other than the one it was created with, which is
  every clear here, thousands of times in `gfx_pacing`. SDL takes that
  value at creation (`SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_R_FLOAT` to
  `_A_FLOAT`, `_DEPTH_FLOAT`, `_STENCIL_NUMBER`), the likely route: a
  target's clear color given when it is made.
* **Optional, last: handles checked across `close()`.** A handle from before
  a `close()` and reopen can name a resource of the new device; the game
  never closes and reopens, so this is a documented limit for now.
* **A 2D and 3D immediate-mode layer in Goose** on top of this one, as
  Lobster has, and shader hot reload.
