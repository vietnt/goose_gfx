// copy_buffer_to_texture copies a buffer's bytes into a texture on the GPU.
// A region of an RGBA8 texture is copied from an offset into the buffer and
// the rest keeps what update_texture wrote; an R32F texture is copied whole
// from an offset of one texel; and a buffer a compute pass has just written
// is copied after that pass in the same frame, so the copy sees its values.
import gfx from "../..";
import std;
import os;

fn pixels(px: const u8[:], n: i64) -> u8[>..] {
    var s: u8[>..] = [];
    for x in n {
        format(s, if x > 0 { "  " } else { "" }, px[x * 4], ",", px[x * 4 + 1], ",", px[x * 4 + 2], ",", px[x * 4 + 3]);
    }
    return s;
}

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    var bytes: u8[>..] = [];
    for i in 24 { bytes.push((10 * i + 1) as u8); }
    let src = gfx::buffer(gfx::BUFFER_VERTEX, bytes);

    // 4 x 2, all 7s; texels 1 and 2 of row 1 from bytes 8 to 15.
    let t = gfx::texture2d(4, 2);
    var sevens: u8[>..] = [];
    sevens.resize(32, 7);
    gfx::update_texture(t, sevens);
    gfx::copy_buffer_to_texture(src, 8, t, gfx::Region { x: 1, y: 1, w: 2, h: 1 });
    let px = gfx::read_pixels(t);
    print("row 0: ", pixels(px, 4));
    print("row 1: ", pixels(px[16..32], 4));

    // Three floats from the second one on.
    let floats: f32[4] = [0.5, -2.0, 1024.25, 3.0];
    let fsrc = gfx::buffer(gfx::BUFFER_VERTEX, bytes_of(floats));
    let r = gfx::texture2d(3, 1, gfx::R32F, gfx::SAMPLED);
    gfx::copy_buffer_to_texture(fsrc, 4, r, gfx::Region {});
    var got: f32[>..] = [];
    gfx::append_as(gfx::read_pixels(r), got);
    print("r32f: ", got[0], " ", got[1], " ", got[2]);

    // Compute writes four packed RGBA8 texels, the copy follows it.
    let written = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 16, "");
    let fill = gfx::compute_pipeline(embed_shader("comp", """
        layout(local_size_x = 4) in;
        layout(std430, set = 1, binding = 0) buffer Out { uint o[]; };
        void main() {
            uint x = gl_GlobalInvocationID.x;
            o[x] = (x + 1u) | ((x * 50u) << 8) | (200u << 16) | (255u << 24);
        }
        """));
    let field = gfx::texture2d(4, 1);
    let out: gfx::Buffer[1] = [written];
    gfx::begin_compute(out);
    gfx::bind(fill);
    gfx::dispatch(1, 1, 1);
    gfx::end_compute();
    gfx::copy_buffer_to_texture(written, field);
    gfx::frame();
    print("after compute: ", pixels(gfx::read_pixels(field), 4));
    gfx::check();
    gfx::close();
}
