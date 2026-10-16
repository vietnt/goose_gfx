// Clearing one layer, mip or slice of a target keeps the others: passes one
// after another in one frame clear each to its own colour, and every one
// reads back with its colour, the first ones too. A clear that let SDL give
// the texture fresh memory would lose what the passes before it cleared.
import gfx from "../..";
import std;
import os;

fn clear(t: gfx::Texture, layer: i32, mip: i32, v: float4) {
    var p = gfx::PassDesc { layer: layer, mip: mip, color_value: v };
    p.color[0] = t;
    gfx::begin_pass(p);
    gfx::end_pass();
}

fn first(name: string, t: gfx::Texture, region: gfx::Region) {
    var px: u8[>..] = [];
    px.resize(gfx::region_size(t, region), 0);
    gfx::read_texture(t, region, px);
    print(name, ": ", px[0], ",", px[1], ",", px[2], ",", px[3]);
}

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let usage = gfx::COLOR_TARGET | gfx::SAMPLED;
    let arr = gfx::texture_array(2, 2, 3, gfx::RGBA8, usage);
    let mipped = gfx::texture(gfx::TextureDesc { format: gfx::RGBA8, usage: usage, width: 4,
                                                 height: 4, mips: 3 });
    let vol = gfx::texture3d(2, 2, 2, gfx::RGBA8, usage);
    clear(arr, 0, 0, float4 { 1.0, 0.0, 0.0, 1.0 });
    clear(arr, 1, 0, float4 { 0.0, 1.0, 0.0, 1.0 });
    clear(arr, 2, 0, float4 { 0.0, 0.0, 1.0, 1.0 });
    clear(mipped, 0, 0, float4 { 0.2, 0.4, 0.6, 0.8 });
    clear(mipped, 0, 1, float4 { 0.4, 0.6, 0.8, 1.0 });
    clear(mipped, 0, 2, float4 { 0.6, 0.8, 1.0, 0.2 });
    clear(vol, 0, 0, float4 { 0.8, 0.2, 0.4, 1.0 });
    clear(vol, 1, 0, float4 { 1.0, 0.8, 0.0, 0.6 });
    first("array layer 0", arr, gfx::Region { layer: 0 });
    first("array layer 1", arr, gfx::Region { layer: 1 });
    first("array layer 2", arr, gfx::Region { layer: 2 });
    first("mip 0", mipped, gfx::Region { mip: 0 });
    first("mip 1", mipped, gfx::Region { mip: 1 });
    first("mip 2", mipped, gfx::Region { mip: 2 });
    first("3D slice 0", vol, gfx::Region { z: 0, w: 1, h: 1, d: 1 });
    first("3D slice 1", vol, gfx::Region { z: 1, w: 1, h: 1, d: 1 });
    gfx::check();
    gfx::close();
}
