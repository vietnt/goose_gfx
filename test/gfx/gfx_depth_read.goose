// Depth textures read back as one f32 a texel: rectangles drawn at known
// depths into D32F (forward and reverse-Z), D16 and the screen's depth, read
// whole and by a region off the origin.
import gfx from "../..";
import std;
import os;

// A rectangle in pixels of a target of `size`, at depth z.
struct Rect { r: float4, z: float4 }

fn floats(xs: f32[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for i in xs.len {
        if i > 0 { out.push(' '); }
        format(out, xs[i]);
    }
    return out;
}

// The f32 texels of `region` of depth texture `t`, n of them.
fn read_depth(t: gfx::Texture, region: gfx::Region, n: i64) -> f32[>..] {
    var raw: u8[>..] = [];
    raw.resize(4 * n, 0);
    gfx::read_texture(t, region, raw);
    var out: f32[>..] = [];
    gfx::append_as(raw, out);
    return out;
}

fn rect(x0: f32, y0: f32, x1: f32, y1: f32, z: f32, w: f32, h: f32) {
    gfx::uniforms(gfx::VERTEX, 0, Rect { r: float4 { x0, y0, x1, y1 }, z: float4 { z, w, h, 0.0 } });
    gfx::draw(6);
}

fn depth_pass(d: gfx::Texture, clear: f32) {
    var pass = gfx::PassDesc { depth: d, depth_value: clear };
    gfx::begin_pass(pass);
}

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let vs = embed_shader("vert", """
        layout(set = 1, binding = 0) uniform Rect { vec4 r; vec4 z; };
        void main() {
            int i = gl_VertexIndex;
            float u = (i == 1 || i == 2 || i == 4) ? r.z : r.x;
            float v = (i == 2 || i == 4 || i == 5) ? r.w : r.y;
            gl_Position = vec4(u / z.y * 2.0 - 1.0, 1.0 - v / z.z * 2.0, z.x, 1.0);
        }
        """);
    let fs = embed_shader("frag", """
        layout(location = 0) out vec4 c;
        void main() { c = vec4(1.0); }
        """);
    let less = gfx::pipeline(vs, fs, gfx::PipelineDesc { depth_test: true, depth_write: true,
                                                         depth_compare: gfx::COMPARE_LESS });
    let greater = gfx::pipeline(vs, fs, gfx::PipelineDesc { depth_test: true, depth_write: true,
                                                            depth_compare: gfx::COMPARE_GREATER });

    // D32F, cleared to 1: three rectangles nearer, the last row left alone.
    let d32 = gfx::texture2d(4, 4, gfx::DEPTH32F, gfx::DEPTH_TARGET);
    depth_pass(d32, 1.0);
    gfx::bind(less);
    rect(0.0, 0.0, 2.0, 2.0, 0.25, 4.0, 4.0);
    rect(2.0, 0.0, 4.0, 2.0, 0.5, 4.0, 4.0);
    rect(0.0, 2.0, 4.0, 3.0, 0.75, 4.0, 4.0);
    gfx::end_pass();
    var whole: f32[>..] = [];
    gfx::read_texture(d32, whole);
    print("d32 rows: ", floats(whole[0..4]), " | ", floats(whole[4..8]), " | ",
          floats(whole[8..12]), " | ", floats(whole[12..16]));
    print("d32 2 x 2 at 1,1: ", floats(read_depth(d32, gfx::Region { x: 1, y: 1, w: 2, h: 2 }, 4)));

    // Reverse-Z: cleared to 0, the greater depth wins.
    let rev = gfx::texture2d(2, 1, gfx::DEPTH32F, gfx::DEPTH_TARGET);
    depth_pass(rev, 0.0);
    gfx::bind(greater);
    rect(0.0, 0.0, 2.0, 1.0, 0.25, 2.0, 1.0);
    rect(0.0, 0.0, 1.0, 1.0, 0.75, 2.0, 1.0);
    rect(0.0, 0.0, 2.0, 1.0, 0.5, 2.0, 1.0);
    gfx::end_pass();
    print("reverse-z: ", floats(read_depth(rev, gfx::Region {}, 2)));

    // D16: 0 and 1 are exact in 16 bits.
    let d16 = gfx::texture2d(2, 1, gfx::DEPTH16, gfx::DEPTH_TARGET);
    depth_pass(d16, 1.0);
    gfx::bind(less);
    rect(0.0, 0.0, 1.0, 1.0, 0.0, 2.0, 1.0);
    gfx::end_pass();
    print("d16: ", floats(read_depth(d16, gfx::Region {}, 2)));

    // The screen's depth, after drawing its left half at 0.5.
    gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::bind(less);
    rect(0.0, 0.0, 4.0, 8.0, 0.5, 8.0, 8.0);
    gfx::end_pass();
    print("screen depth at 3,5 and 4,5: ",
          floats(read_depth(gfx::screen_depth(), gfx::Region { x: 3, y: 5, w: 2, h: 1 }, 2)));
    gfx::close();
}
