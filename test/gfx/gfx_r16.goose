// R16, RG16 and RGBA16, 16-bit unsigned normalized channels: uploaded and
// read back as written, sampled to their exact values, and drawn into, as a
// picker draws depth into one. 0x1234 (4660) and the rest are values no
// 8-bit format holds; each channel of each texel differs, so a swapped
// channel or texel shows.
import gfx from "../..";
import std;
import os;

struct Texels { t0: float4, t1: float4 }

fn main() {
    guard gfx::open_headless(2, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
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
        void main() { c = round(texture(tex, gl_FragCoord.xy / vec2(2.0, 1.0)) * 65535.0); }
        """));
    // The values given, drawn over a clear to others.
    let draw = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 3, binding = 0) uniform Texels { vec4 t[2]; };
        void main() { c = t[int(gl_FragCoord.x)] / 65535.0; }
        """));
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    let all: u16[8] = [0x1234, 1, 0xABCD, 65535, 7, 65535, 0, 0x8000];
    let formats: i32[3] = [gfx::R16, gfx::RG16, gfx::RGBA16];
    let channels: i64[3] = [1, 2, 4];
    for i in 3 {
        let n = channels[i];
        let name = if i == 0 { "R16" } else if i == 1 { "RG16" } else { "RGBA16" };
        var values: u16[>..] = [];
        for t in 2 { for ch in n { values.push(all[t * 4 + ch]); } }
        let t = gfx::texture2d(2, 1, formats[i], gfx::SAMPLED);
        gfx::update_texture(t, bytes_of(values));
        var back: u16[>..] = [];
        gfx::read_texture(t, back);
        print(name, " uploaded and read back: ", back);

        let floats = gfx::render_target(2, 1, gfx::RGBA32F);
        gfx::begin_pass(floats, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
        gfx::bind(sample);
        gfx::bind_texture(gfx::FRAGMENT, 0, t, nearest);
        gfx::draw(3);
        gfx::end_pass();
        var texels: f32[>..] = [];
        gfx::read_texture(floats, texels);
        var sampled: f32[>..] = [];
        for x in 2 { for ch in n { sampled.push(texels[x * 4 + ch]); } }
        print(name, " sampled, times 65535: ", sampled);

        let target = gfx::render_target(2, 1, formats[i]);
        gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.5, 0.25, 0.75, 0.125 });
        gfx::bind(draw);
        gfx::uniforms(gfx::FRAGMENT, 0, Texels {
            t0: float4 { all[0], all[1], all[2], all[3] },
            t1: float4 { all[4], all[5], all[6], all[7] } });
        gfx::draw(3);
        gfx::end_pass();
        var drawn: u16[>..] = [];
        gfx::read_texture(target, drawn);
        print(name, " drawn: ", drawn);
        gfx::release(t);
        gfx::release(floats);
        gfx::release(target);
    }
    gfx::close();
}
