// A 4x multisampled texture reads back resolved. A rectangle ends at x =
// 1.5 of four pixels: pixel 0 is covered, pixel 1 has two of its four
// standard sample positions covered, and pixels 2 and 3 none, so they
// resolve to the colour, the exact mean of the colour and the clear, and
// the clear. Read at once and without waiting; then a pass that resolves
// the texture through a target of its own gets the same, so reading kept
// the samples.
import gfx from "../..";
import std;
import os;

struct Color { c: float4 }

fn texels(name: string, px: u8[:]) {
    print(name, ": ", px[0], ",", px[1], ",", px[2], ",", px[3], "  ", px[4], ",", px[5], ",",
          px[6], ",", px[7], "  ", px[8], ",", px[9], ",", px[10], ",", px[11], "  ", px[12], ",",
          px[13], ",", px[14], ",", px[15]);
}

fn main() {
    guard gfx::open_headless(4, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let rect = gfx::pipeline(embed_shader("vert", """
        void main() {
            const vec2 p[6] = vec2[6](vec2(-1.0, -1.0), vec2(-0.25, -1.0), vec2(-0.25, 1.0),
                                      vec2(-1.0, -1.0), vec2(-0.25, 1.0), vec2(-1.0, 1.0));
            gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
        }
        """), embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 3, binding = 0) uniform Color { vec4 color; };
        void main() { c = color; }
        """));
    let ms = gfx::texture(gfx::TextureDesc { format: gfx::RGBA8, usage: gfx::COLOR_TARGET,
                                             width: 4, height: 1, samples: 4 });
    var draw = gfx::PassDesc { color_value: float4 { 0.2, 0.2, 0.2, 1.0 } };
    draw.color[0] = ms;
    gfx::begin_pass(draw);
    gfx::bind(rect);
    gfx::uniforms(gfx::FRAGMENT, 0, Color { c: float4 { 1.0, 0.2, 0.6, 1.0 } });
    gfx::draw(6);
    gfx::end_pass();

    texels("read", gfx::read_pixels(ms));
    let later = gfx::read_texture_async(ms);
    gfx::frame();
    texels("read without waiting", gfx::take(later));

    let rt = gfx::render_target(4, 1, gfx::RGBA8);
    var resolve = gfx::PassDesc { clear_color: false };
    resolve.color[0] = ms;
    resolve.resolve[0] = rt;
    gfx::begin_pass(resolve);
    gfx::end_pass();
    texels("resolved by a pass after", gfx::read_pixels(rt));

    gfx::check();
    gfx::close();
}
