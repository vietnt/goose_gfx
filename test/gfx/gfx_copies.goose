// copy_buffer and copy_texture copy on the GPU, in the order recorded. Each
// copy moves a part to somewhere else in its destination, so an offset or a
// size taken from the wrong side shows, and every texel differs in every
// channel: from a texture region into another's corner, into a mip and
// layer of an array and from there to another layer, into a slice of a 3D
// texture, an R32F texture whole, buffer ranges, within one buffer, and a
// whole buffer.
import gfx from "../..";
import std;
import os;

// Texel p of the source: bytes no other texel or fill has.
fn texel(p: i64) -> u8[4] { [(p + 1) as u8, (p + 101) as u8, (200 - p) as u8, (250 - p) as u8] }

fn fill(n: i64, v: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in n * 4 { out.push(v as u8); }
    return out;
}

fn texels(name: string, px: u8[:]) {
    write_stdout(str(name, ":"));
    for i in px.len / 4 {
        write_stdout(str(" ", px[i * 4], ",", px[i * 4 + 1], ",", px[i * 4 + 2], ",", px[i * 4 + 3]));
    }
    write_stdout("\n");
}

fn main() {
    guard gfx::open_headless(4, 2) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    var bytes: u8[>..] = [];
    for p in 8 {
        let t = texel(p);
        for c in 4 { bytes.push(t[c]); }
    }
    let src = gfx::texture2d(4, 2);
    gfx::update_texture(src, bytes);

    // Columns 1 and 2 of both rows into columns 2 and 3.
    let dst = gfx::texture2d(4, 2);
    gfx::update_texture(dst, fill(8, 7));
    gfx::copy_texture(src, gfx::Region { x: 1, w: 2, h: 2 }, dst, gfx::Region { x: 2 });
    let d = gfx::read_pixels(dst);
    texels("region, row 0", d[0..16]);
    texels("region, row 1", d[16..32]);

    // The second row's first two texels into mip 1 (2 x 1) of layer 1, and
    // that within the array into mip 1 of layer 0.
    let arr = gfx::texture(gfx::TextureDesc { kind: gfx::TEXTURE_2D_ARRAY, format: gfx::RGBA8,
                                              usage: gfx::SAMPLED, width: 4, height: 2,
                                              depth: 2, mips: 2 });
    let at = gfx::Region { mip: 1, layer: 1 };
    gfx::update_texture(arr, at, fill(2, 9));
    gfx::copy_texture(src, gfx::Region { y: 1, w: 2, h: 1 }, arr, at);
    var a: u8[>..] = [];
    a.resize(8, 0);
    gfx::read_texture(arr, at, a);
    texels("array mip 1 layer 1", a);
    let at0 = gfx::Region { mip: 1 };
    gfx::update_texture(arr, at0, fill(2, 8));
    gfx::copy_texture(arr, at, arr, at0);
    gfx::read_texture(arr, at0, a);
    texels("array mip 1 layer 0", a);

    // The first row's last two texels into slice 2 of three.
    let vol = gfx::texture3d(2, 1, 3, gfx::RGBA8, gfx::SAMPLED);
    var slices: u8[>..] = [];
    for z in 3 {
        for x in 2 {
            for c in 4 { slices.push((50 + z * 10 + x) as u8); }
        }
    }
    gfx::update_texture(vol, slices);
    gfx::copy_texture(src, gfx::Region { x: 2, w: 2, h: 1 }, vol, gfx::Region { z: 2 });
    var v: u8[>..] = [];
    v.resize(24, 0);
    gfx::read_texture(vol, gfx::Region {}, v);
    texels("3D slices 0 to 2", v);

    let f1 = gfx::texture2d(2, 1, gfx::R32F, gfx::SAMPLED);
    let f2 = gfx::texture2d(2, 1, gfx::R32F, gfx::SAMPLED);
    let fs: f32[2] = [-2.0, 1024.25];
    gfx::update_texture(f1, bytes_of(fs));
    gfx::copy_texture(f1, f2);
    var f: f32[>..] = [];
    gfx::read_texture(f2, f);
    print("r32f whole: ", f[0], " ", f[1]);

    // Words 2 to 4 of a into words 1 to 3 of b; then within b, words 0 and
    // 1 into 6 and 7; then all of a into the start of a larger c.
    let words: u32[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    let others: u32[8] = [100, 101, 102, 103, 104, 105, 106, 107];
    let a_buf = gfx::buffer(gfx::BUFFER_STORAGE, bytes_of(words));
    let b_buf = gfx::buffer(gfx::BUFFER_STORAGE, bytes_of(others));
    let c_buf = gfx::buffer(gfx::BUFFER_STORAGE, 48, "");
    gfx::copy_buffer(a_buf, 8, b_buf, 4, 12);
    gfx::copy_buffer(b_buf, 0, b_buf, 24, 8);
    gfx::copy_buffer(a_buf, c_buf);
    var b: u32[>..] = [];
    gfx::read_buffer(b_buf, b, 8);
    print("buffer ranges: ", b);
    var c: u32[>..] = [];
    gfx::read_buffer(c_buf, c, 12);
    print("whole buffer: ", c);

    gfx::check();
    gfx::close();
}
