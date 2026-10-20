// A region bigger than one staging buffer goes in parts, and arrives whole.
// An RGBA8 texture of 144 MB, each of whose rows is shifted from the last,
// reads back with no byte off and the same position-weighted checksum as
// what was uploaded. A BC3 texture of 136 MB, staged in parts of whole
// block rows, has a solid colour in each 4 x 4 block made from the block's
// row and column; every block is fetched, and each must give back its own
// 5:6:5 colour, so a part landing a row off or a block row cut in two shows.
import gfx from "../..";
import std;
import os;

// What reading back can tell apart: each byte weighted by where it is.
fn checksum(b: u8[:]) -> i64 {
    var sum = 0;
    for i in b.len { sum += b[i] * (i % 251 + 1); }
    sum
}

// The 5:6:5 colour of block bx, by.
fn block_colour(bx: i64, by: i64) -> i64 { (bx * 7 + by * 13) & 0xFFFF }

fn main() {
    guard gfx::open_headless(8, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let info = gfx::upload_info();

    let w = 8192;
    let h = 4400;
    var bytes: u8[>..] = [];
    bytes.resize(w * h * 4, 0);
    for i in bytes.len { bytes[i] = ((i + (i >> 15) * 37) & 255) as u8; }
    print("RGBA8 region bigger than a staging buffer: ", bytes.len > info.capacity);
    let t = gfx::texture2d(w, h);
    gfx::update_texture(t, bytes);
    let back = gfx::read_pixels(t);
    var off = 0;
    for i in bytes.len { if back[i] != bytes[i] { off++; } }
    print("bytes off: ", off, ", checksum ", checksum(back), ", uploaded ", checksum(bytes));
    gfx::release(t);

    let bw = 4096;
    let bh = 2080;
    var blocks: u8[>..] = [];
    blocks.resize(bw * bh * 16, 0);
    for by in bh {
        for bx in bw {
            let at = (by * bw + bx) * 16;
            let c = block_colour(bx, by);
            blocks[at] = 255;
            blocks[at + 1] = 255;
            blocks[at + 8] = (c & 255) as u8;
            blocks[at + 9] = (c >> 8) as u8;
        }
    }
    print("BC3 region bigger than a staging buffer: ", blocks.len > info.capacity);
    let bc = gfx::texture(gfx::TextureDesc { format: gfx::BC3, width: (bw * 4) as i32,
                                             height: (bh * 4) as i32 });
    gfx::update_texture(bc, blocks);
    // One fragment per block, fetching a texel inside it.
    let probe = gfx::pipeline(embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """), embed_shader("frag", """
        layout(set = 2, binding = 0) uniform sampler2D tex;
        layout(location = 0) out uint c;
        void main() {
            vec4 v = texelFetch(tex, ivec2(gl_FragCoord.xy) * 4 + ivec2(1, 2), 0);
            c = (uint(round(v.r * 31.0)) << 11) | (uint(round(v.g * 63.0)) << 5) | uint(round(v.b * 31.0));
        }
        """));
    let target = gfx::texture2d(bw, bh, gfx::R32UI, gfx::COLOR_TARGET);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(probe);
    gfx::bind_texture(gfx::FRAGMENT, 0, bc, gfx::sampler(gfx::NEAREST, gfx::CLAMP));
    gfx::draw(3);
    gfx::end_pass();
    var got: u32[>..] = [];
    gfx::read_texture(target, got);
    var wrong = 0;
    var first = -1;
    for i in bw * bh {
        if got[i] != block_colour(i % bw, i / bw) {
            if first < 0 { first = i; }
            wrong++;
        }
    }
    print("BC3 blocks that sampled another colour: ", wrong, " of ", bw * bh);
    if first >= 0 { print("the first is block ", first % bw, ",", first / bw, ": ", got[first]); }
    gfx::check();
    gfx::close();
}
