// R16, one 16-bit unsigned normalized channel: uploaded and read back as
// written, sampled to its exact value, and drawn into, as a picker draws
// depth into one. 0x1234 (4660) is one no 8-bit format holds.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(4, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let values: u16[4] = [0, 1, 0x1234, 65535];
    let t = gfx::texture2d(4, 1, gfx::R16, gfx::SAMPLED);
    gfx::update_texture(t, bytes_of(values));
    var back: u16[>..] = [];
    gfx::read_texture(t, back);
    print("uploaded and read back: ", back);

    let vs = embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """);
    // Each pixel samples the texel it covers, at its centre.
    let sample = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2D tex;
        void main() { c = vec4(round(texture(tex, gl_FragCoord.xy / vec2(4.0, 1.0)).r * 65535.0)); }
        """));
    let floats = gfx::render_target(4, 1, gfx::R32F);
    gfx::begin_pass(floats, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(sample);
    gfx::bind_texture(gfx::FRAGMENT, 0, t, gfx::sampler(gfx::NEAREST, gfx::CLAMP));
    gfx::draw(3);
    gfx::end_pass();
    var sampled: f32[>..] = [];
    gfx::read_texture(floats, sampled);
    print("sampled, times 65535: ", sampled);

    // The same values drawn, over a clear to another.
    let draw = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) out vec4 c;
        void main() {
            float v[4] = float[4](0.0, 1.0, 4660.0, 65535.0);
            c = vec4(v[int(gl_FragCoord.x)] / 65535.0);
        }
        """));
    let target = gfx::render_target(4, 1, gfx::R16);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.5, 0.0, 0.0, 0.0 });
    gfx::bind(draw);
    gfx::draw(3);
    gfx::end_pass();
    var drawn: u16[>..] = [];
    gfx::read_texture(target, drawn);
    print("drawn: ", drawn);
    gfx::close();
}
