// A compute pass that cycles what it writes: two frames each rewrite all of
// a storage texture and a storage buffer, frame 2 while frame 1's sampling
// of the texture and its read of the buffer may still be in flight. Each
// frame's draw and read back must see that frame's values, nothing being
// waited for until both frames are submitted.
import gfx from "../..";
import std;
import os;

struct Value { v: u32, pad 12 }

fn main() {
    guard gfx::open_headless(4, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let field = gfx::texture2d(4, 1, gfx::RGBA8, gfx::COMPUTE_WRITE | gfx::SAMPLED);
    let counts = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 16, "");
    let write = gfx::compute_pipeline(embed_shader("comp", """
        layout(local_size_x = 4) in;
        layout(set = 1, binding = 0, rgba8) uniform writeonly image2D field;
        layout(std430, set = 1, binding = 1) buffer Counts { uint counts[]; };
        layout(set = 2, binding = 0) uniform Value { uint v; };
        void main() {
            uint x = gl_GlobalInvocationID.x;
            imageStore(field, ivec2(x, 0), vec4(float(v), float(x), 0.0, 255.0) / 255.0);
            counts[x] = v * 100u + x;
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
    var reads: gfx::Readback[>..] = [];
    let bufs: gfx::Buffer[1] = [counts];
    let texs: gfx::Texture[1] = [field];
    for f in 2 {
        gfx::begin_compute(bufs, texs, true);
        gfx::bind(write);
        gfx::uniforms(gfx::COMPUTE, 0, Value { v: (10 * (f + 1)) as u32 });
        gfx::dispatch(1, 1, 1);
        gfx::end_compute();
        gfx::begin_pass(targets[f], gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
        gfx::bind(sample);
        gfx::bind_texture(gfx::FRAGMENT, 0, field, nearest);
        gfx::draw(3);
        gfx::end_pass();
        reads.push(gfx::read_buffer_async(counts, 0, 16));
        gfx::frame();
    }
    for f in 2 {
        let px = gfx::read_pixels(targets[f]);
        var line: u8[>..] = [];
        for x in 4 {
            format(line, if x > 0 { "  " } else { "" }, px[x * 4], ",", px[x * 4 + 1], ",", px[x * 4 + 2], ",", px[x * 4 + 3]);
        }
        var got: u32[>..] = [];
        gfx::take(reads[f], got);
        print("frame ", f + 1, ": sampled ", line, "; buffer ", got);
    }
    gfx::close();
}
