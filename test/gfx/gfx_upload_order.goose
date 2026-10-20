// Updates keep their order with the draws around them, through the upload
// ring too. In one frame, a 1 x 1 texture is updated three times, each
// update followed by a draw that samples it into a target of its own.
// Between the second and the third, 320 MB of other updates fill both
// staging buffers and come back round to the one holding the second
// update's bytes, so the frame's commands go to the GPU early and the ring
// waits before writing over them. Each target reads back the update before
// its draw, not a later one, nor the filler's bytes.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let sample = gfx::pipeline(embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """), embed_shader("frag", """
        layout(set = 2, binding = 0) uniform sampler2D tex;
        layout(location = 0) out vec4 c;
        void main() { c = texelFetch(tex, ivec2(0, 0), 0); }
        """));
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    let t = gfx::texture2d(1, 1);
    let filler = gfx::texture2d(4096, 4096);
    var filling: u8[>..] = [];
    filling.resize(4096 * 4096 * 4, 7);
    let values: u8[12] = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120];
    var targets: gfx::Texture[3] = [gfx::Texture { 0 }; 3];
    gfx::frame();
    for k in 3 {
        if k == 2 {
            for i in 5 { gfx::update_texture(filler, filling); }
        }
        gfx::update_texture(t, values[k * 4 .. k * 4 + 4]);
        targets[k] = gfx::render_target(1, 1, gfx::RGBA8);
        gfx::begin_pass(targets[k], gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
        gfx::bind(sample);
        gfx::bind_texture(gfx::FRAGMENT, 0, t, nearest);
        gfx::draw(3);
        gfx::end_pass();
    }
    let info = gfx::upload_info();
    print("the filler took more than the ring: ", 5 * filling.len > 2 * info.buffer_bytes);
    gfx::frame();
    for k in 3 {
        let px = gfx::read_pixels(targets[k]);
        print("draw ", k, " read ", px[0], ",", px[1], ",", px[2], ",", px[3]);
    }
    gfx::check();
    gfx::close();
}
