// A texture written by compute, then moved by SDL while the frame that wrote
// it is still being recorded. Released textures leave holes in a memory
// block, the field texture is placed in that block, a compute pass fills it,
// and then, inside the same frame, a large buffer that fits in no existing
// block is created with contents, which goose_gfx uploads through a command
// buffer of its own. On Vulkan, a submit after such an allocation compacts a
// fragmented block. It must not move a texture that the frame being
// recorded writes: later frames would sample the copy, made before the
// compute pass ran. Every sample and readback must see the compute values.
import gfx from "../..";
import std;
import os;

struct Value { v: u32, pad 12 }

fn line(px: const u8[:], n: i64) -> u8[>..] {
    var s: u8[>..] = [];
    for x in n {
        format(s, if x > 0 { "  " } else { "" }, px[x * 4], ",", px[x * 4 + 1], ",", px[x * 4 + 2], ",", px[x * 4 + 3]);
    }
    return s;
}

fn main() {
    guard gfx::open_headless(4, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    var short: gfx::Texture[>..] = [];
    for i in 8 { short.push(gfx::texture2d(256, 256)); }
    for i in 8 { if i % 2 == 1 { gfx::release(short[i]); } }
    gfx::flush();
    let field = gfx::texture2d(4, 1, gfx::RGBA8, gfx::COMPUTE_WRITE | gfx::SAMPLED);
    let write = gfx::compute_pipeline(embed_shader("comp", """
        layout(local_size_x = 4) in;
        layout(set = 1, binding = 0, rgba8) uniform writeonly image2D field;
        layout(set = 2, binding = 0) uniform Value { uint v; };
        void main() {
            uint x = gl_GlobalInvocationID.x;
            imageStore(field, ivec2(x, 0), vec4(float(v + x), float(x * 3u), 200.0, 255.0) / 255.0);
        }
        """));
    let sample = gfx::pipeline(embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """), embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2D field;
        void main() { c = texture(field, gl_FragCoord.xy / vec2(4.0, 1.0)); }
        """));
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    let targets: gfx::Texture[2] = [gfx::render_target(4, 1, gfx::RGBA8), gfx::render_target(4, 1, gfx::RGBA8)];
    let texs: gfx::Texture[1] = [field];
    let bufs: gfx::Buffer[>..] = [];
    gfx::begin_compute(bufs, texs, false);
    gfx::bind(write);
    gfx::uniforms(gfx::COMPUTE, 0, Value { v: 40 });
    gfx::dispatch(1, 1, 1);
    gfx::end_compute();
    var big: u8[>..] = [];
    big.resize(4 << 20, 0);
    let late = gfx::buffer(gfx::BUFFER_VERTEX, big);
    for i in 4 {
        var small: u8[>..] = [];
        small.resize(16, 0);
        gfx::buffer(gfx::BUFFER_VERTEX, small);
    }
    for f in 2 {
        gfx::begin_pass(targets[f], gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
        gfx::bind(sample);
        gfx::bind_texture(gfx::FRAGMENT, 0, field, nearest);
        gfx::draw(3);
        gfx::end_pass();
        gfx::frame();
    }
    for f in 2 { print("frame ", f + 1, " sampled: ", line(gfx::read_pixels(targets[f]), 4)); }
    print("read back: ", line(gfx::read_pixels(field), 4));
    gfx::release(late);
    gfx::close();
}
