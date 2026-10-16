// Depth reads back from any mip of a 2D texture, from a layer of an array
// and from a face of a cube, and a pass draws depth into each of those. Each
// is cleared to a depth of its own and then has its top left texel drawn
// nearer, at another depth of its own, so a mip, layer or face read from
// the wrong place, or a face read flipped, shows. Regions off the origin
// read from a mip above 0 and from a face.
import gfx from "../..";
import std;
import os;

struct Rect { r: float4, z: float4 }

fn floats(xs: f32[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for i in xs.len {
        if i > 0 { out.push(' '); }
        format(out, xs[i]);
    }
    return out;
}

fn read_depth(t: gfx::Texture, region: gfx::Region) -> f32[>..] {
    var raw: u8[>..] = [];
    raw.resize(gfx::region_size(t, region), 0);
    gfx::read_texture(t, region, raw);
    var out: f32[>..] = [];
    gfx::append_as(raw, out);
    return out;
}

// Clears `layer` and `mip` of `t`, `size` texels square, to `clear`, and
// draws its top left texel at `near`.
fn fill(t: gfx::Texture, layer: i32, mip: i32, size: f32, clear: f32, near: f32, p: gfx::Pipeline) {
    gfx::begin_pass(gfx::PassDesc { depth: t, depth_value: clear, depth_layer: layer, depth_mip: mip });
    gfx::bind(p);
    gfx::uniforms(gfx::VERTEX, 0, Rect { r: float4 { 0.0, 0.0, 1.0, 1.0 },
                                          z: float4 { near, size, size, 0.0 } });
    gfx::draw(6);
    gfx::end_pass();
}

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let less = gfx::pipeline(embed_shader("vert", """
        layout(set = 1, binding = 0) uniform Rect { vec4 r; vec4 z; };
        void main() {
            int i = gl_VertexIndex;
            float u = (i == 1 || i == 2 || i == 4) ? r.z : r.x;
            float v = (i == 2 || i == 4 || i == 5) ? r.w : r.y;
            gl_Position = vec4(u / z.y * 2.0 - 1.0, 1.0 - v / z.z * 2.0, z.x, 1.0);
        }
        """), embed_shader("frag", "void main() {}"), gfx::PipelineDesc { depth_test: true, depth_write: true,
                                 depth_compare: gfx::COMPARE_LESS });
    let usage = gfx::DEPTH_TARGET | gfx::SAMPLED;

    let mipped = gfx::texture(gfx::TextureDesc { format: gfx::DEPTH32F, usage: usage, width: 4,
                                                 height: 4, mips: 3 });
    fill(mipped, 0, 0, 4.0, 1.0, 0.75, less);
    fill(mipped, 0, 1, 2.0, 0.5, 0.375, less);
    fill(mipped, 0, 2, 1.0, 0.25, 0.125, less);
    print("2D mip 0, first row: ", floats(read_depth(mipped, gfx::Region { mip: 0, w: 4, h: 1 })));
    print("2D mip 1: ", floats(read_depth(mipped, gfx::Region { mip: 1 })));
    print("2D mip 2: ", floats(read_depth(mipped, gfx::Region { mip: 2 })));
    print("2D mip 1 at 1,0: ", floats(read_depth(mipped, gfx::Region { mip: 1, x: 1, w: 1, h: 1 })));

    let arr = gfx::texture(gfx::TextureDesc { kind: gfx::TEXTURE_2D_ARRAY, format: gfx::DEPTH32F,
                                              usage: usage, width: 2, height: 2, depth: 3,
                                              mips: 2 });
    for l in 3 {
        fill(arr, l as i32, 0, 2.0, 0.25 * (l + 1), 0.125 * (l + 1), less);
        gfx::begin_pass(gfx::PassDesc { depth: arr, depth_value: 0.0625 * (l + 1),
                                        depth_layer: l as i32, depth_mip: 1 });
        gfx::end_pass();
    }
    for l in 3 {
        print("array layer ", l, ": mip 0 ", floats(read_depth(arr, gfx::Region { layer: l as i32 })),
              ", mip 1 ", floats(read_depth(arr, gfx::Region { mip: 1, layer: l as i32 })));
    }

    let cube = gfx::texture(gfx::TextureDesc { kind: gfx::TEXTURE_CUBE, format: gfx::DEPTH32F,
                                               usage: usage, width: 2, height: 2, mips: 2 });
    for f in 6 {
        fill(cube, f as i32, 0, 2.0, 0.5 + 0.0625 * f, 0.0625 * (f + 1), less);
        gfx::begin_pass(gfx::PassDesc { depth: cube, depth_value: 0.875 + 0.015625 * f,
                                        depth_layer: f as i32, depth_mip: 1 });
        gfx::end_pass();
    }
    for f in 6 {
        print("cube face ", f, ": mip 0 ", floats(read_depth(cube, gfx::Region { layer: f as i32 })),
              ", mip 1 ", floats(read_depth(cube, gfx::Region { mip: 1, layer: f as i32 })));
    }
    print("cube face 3 at 1,1: ",
          floats(read_depth(cube, gfx::Region { layer: 3, x: 1, y: 1, w: 1, h: 1 })));

    gfx::check();
    gfx::close();
}
