// Block-compressed textures: blocks built by hand, uploaded by mip, layer
// and region, fetched texel by texel into RGBA8 and read back exactly, and
// their raw blocks read back as written. Only what every decoder gets
// exactly is checked: BC1 and BC3 colors at their two endpoints and BC1's
// transparent black (decoders round the thirds between endpoints
// differently), BC3 alpha, BC4 and BC5 at their endpoints and the 0 and 255
// of their six-value mode, and BC7 mode 6, whose interpolation the format
// defines in integers. Direct3D 12 refuses the raw reads, with
// gfx_compressed.direct3d12.out.
import gfx from "../..";
import std;
import os;

struct Lod { lod: i32, layer: i32, pad 8 }

let bc1_mip0: u8[32] = [255, 255, 31, 0, 20, 65, 80, 5, 0, 248, 0, 0, 80, 5, 65, 20, 31, 0, 0, 248, 244, 241, 80, 79, 224, 7, 31, 0, 68, 68, 68, 68];
let bc1_mip1: u8[8] = [255, 255, 0, 0, 65, 65, 65, 65];
let bc1_mip2: u8[8] = [0, 248, 224, 7, 4, 1, 0, 0];
let bc1_mip3: u8[8] = [31, 0, 0, 0, 1, 0, 0, 0];
let bc3: u8[16] = [60, 180, 136, 143, 248, 136, 143, 248, 255, 255, 31, 0, 20, 65, 80, 5];
let bc4: u8[8] = [40, 200, 136, 127, 7, 192, 159, 216];
let bc5: u8[16] = [10, 250, 136, 143, 248, 136, 143, 248, 90, 130, 119, 112, 7, 119, 112, 7];
let bc7_mip0: u8[96] = [64, 114, 129, 66, 150, 253, 255, 120, 81, 250, 148, 62, 216, 114, 28, 182, 192, 117, 161, 242, 150, 253, 255, 249, 98, 11, 165, 79, 233, 131, 45, 199, 64, 121, 193, 162, 151, 253, 255, 122, 117, 28, 182, 80, 250, 148, 62, 216, 192, 124, 225, 82, 144, 253, 255, 251, 134, 45, 199, 97, 11, 165, 79, 233, 64, 64, 1, 3, 145, 253, 255, 124, 153, 62, 216, 114, 28, 182, 80, 250, 192, 67, 33, 179, 145, 253, 255, 253, 170, 79, 233, 131, 45, 199, 97, 11];
let bc7_mip1: u8[32] = [64, 71, 65, 99, 146, 253, 255, 126, 189, 80, 250, 148, 62, 216, 114, 28, 192, 74, 97, 19, 147, 253, 255, 255, 206, 97, 11, 165, 79, 233, 131, 45];
let bc7_edge: u8[16] = [64, 78, 129, 195, 147, 253, 255, 120, 209, 114, 28, 182, 80, 250, 148, 62];
let bc7_layer1: u8[16] = [192, 63, 0, 240, 7, 2, 255, 255, 241, 135, 240, 135, 240, 135, 240, 135];

fn texels(px: u8[:], w: i64, channels: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in px.len / 4 {
        out.append(if i % w == 0 { "\n  " } else { " " });
        for c in channels {
            if c > 0 { out.push(','); }
            format(out, px[i * 4 + c]);
        }
    }
    return out;
}

// Where SDL cannot download a compressed texture (Direct3D 12) the read is
// refused, and the line says why.
fn raw_read(what: const u8[:], t: gfx::Texture, region: gfx::Region, written: u8[:]) {
    var raw: u8[>..] = [];
    raw.resize(written.len, 0);
    if gfx::read_texture(t, region, raw) {
        print("raw: ", what, " as written ", same(raw, written));
    } else {
        print("raw: ", what, " refused: ", gfx::error());
    }
}

fn same(a: u8[:], b: u8[:]) -> bool {
    if a.len != b.len { return false; }
    for i in a.len {
        if a[i] != b[i] { return false; }
    }
    return true;
}

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let vs = embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
        }
        """);
    let fetch2d = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2D t;
        layout(set = 3, binding = 0) uniform Lod { int lod; int layer; };
        void main() { c = texelFetch(t, ivec2(gl_FragCoord.xy), lod); }
        """));
    let fetch_array = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2DArray t;
        layout(set = 3, binding = 0) uniform Lod { int lod; int layer; };
        void main() { c = texelFetch(t, ivec3(ivec2(gl_FragCoord.xy), layer), lod); }
        """));
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    // Mip `lod` (of layer `layer`) of t, w x h, texel for texel.
    fn fetch(p: gfx::Pipeline, t: gfx::Texture, s: gfx::Sampler, lod: i64, layer: i64, w: i64,
             h: i64) -> u8[>..] {
        let target = gfx::render_target(w, h, gfx::RGBA8);
        gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.5, 0.5, 0.5, 0.5 });
        gfx::bind(p);
        gfx::bind_texture(gfx::FRAGMENT, 0, t, s);
        gfx::uniforms(gfx::FRAGMENT, 0, Lod { lod: lod as i32, layer: layer as i32 });
        gfx::draw(3);
        gfx::end_pass();
        let px = gfx::read_pixels(target);
        gfx::release(target);
        return px;
    }

    // BC1, 8 x 8 with its whole chain: mips 2 and 1 are less than a block.
    let bc1_tex = gfx::texture(gfx::TextureDesc { format: gfx::BC1, width: 8, height: 8, mips: 4 });
    guard bc1_tex.id != 0 else {
        write_stderr(str("gfx: no GPU device with BC formats: ", gfx::error(), "\n"));
        return;
    }
    gfx::update_texture(bc1_tex, gfx::Region { mip: 0 }, bc1_mip0);
    gfx::update_texture(bc1_tex, gfx::Region { mip: 1 }, bc1_mip1);
    gfx::update_texture(bc1_tex, gfx::Region { mip: 2 }, bc1_mip2);
    gfx::update_texture(bc1_tex, gfx::Region { mip: 3 }, bc1_mip3);
    print("bc1 mip 0:", texels(fetch(fetch2d, bc1_tex, nearest, 0, 0, 8, 8), 8, 4));
    print("bc1 mip 1:", texels(fetch(fetch2d, bc1_tex, nearest, 1, 0, 4, 4), 4, 4));
    print("bc1 mip 2 (2 x 2 of a block):", texels(fetch(fetch2d, bc1_tex, nearest, 2, 0, 2, 2), 2, 4));
    print("bc1 mip 3 (1 x 1):", texels(fetch(fetch2d, bc1_tex, nearest, 3, 0, 1, 1), 1, 4));

    let bc3_tex = gfx::texture(gfx::TextureDesc { format: gfx::BC3, width: 4, height: 4 });
    gfx::update_texture(bc3_tex, bc3);
    print("bc3:", texels(fetch(fetch2d, bc3_tex, nearest, 0, 0, 4, 4), 4, 4));
    let bc4_tex = gfx::texture(gfx::TextureDesc { format: gfx::BC4, width: 4, height: 4 });
    gfx::update_texture(bc4_tex, bc4);
    print("bc4 r:", texels(fetch(fetch2d, bc4_tex, nearest, 0, 0, 4, 4), 4, 1));
    let bc5_tex = gfx::texture(gfx::TextureDesc { format: gfx::BC5, width: 4, height: 4 });
    gfx::update_texture(bc5_tex, bc5);
    print("bc5 r,g:", texels(fetch(fetch2d, bc5_tex, nearest, 0, 0, 4, 4), 4, 2));

    // BC7, 12 x 8: mip 1 is 6 x 4, its second block cut to 2 texels wide.
    let bc7_tex = gfx::texture(gfx::TextureDesc { format: gfx::BC7, width: 12, height: 8, mips: 2 });
    gfx::update_texture(bc7_tex, gfx::Region { mip: 0 }, bc7_mip0);
    gfx::update_texture(bc7_tex, gfx::Region { mip: 1 }, bc7_mip1);
    print("bc7 12 x 8 mip 0:", texels(fetch(fetch2d, bc7_tex, nearest, 0, 0, 12, 8), 12, 4));
    print("bc7 mip 1 (6 x 4):", texels(fetch(fetch2d, bc7_tex, nearest, 1, 0, 6, 4), 6, 4));
    gfx::update_texture(bc7_tex, gfx::Region { mip: 1, x: 4, w: 2, h: 4 }, bc7_edge);
    print("bc7 mip 1 after the edge block at 4,0 is replaced:",
          texels(fetch(fetch2d, bc7_tex, nearest, 1, 0, 6, 4), 6, 4));

    let layers = gfx::texture(gfx::TextureDesc { kind: gfx::TEXTURE_2D_ARRAY, format: gfx::BC7,
                                                 width: 4, height: 4, depth: 2 });
    gfx::update_texture(layers, gfx::Region { layer: 0 }, bc7_edge);
    gfx::update_texture(layers, gfx::Region { layer: 1 }, bc7_layer1);
    print("bc7 array layer 1:", texels(fetch(fetch_array, layers, nearest, 0, 1, 4, 4), 4, 4));

    // The blocks themselves, read back: region_size counts whole blocks.
    print("raw: bc1 mip 2 is ", gfx::region_size(bc1_tex, gfx::Region { mip: 2 }), " bytes, ",
          "bc7 mip 0 ", gfx::region_size(bc7_tex, gfx::Region {}), ", its edge block ",
          gfx::region_size(bc7_tex, gfx::Region { mip: 1, x: 4, w: 2, h: 4 }));
    raw_read("bc1 mip 2", bc1_tex, gfx::Region { mip: 2 }, bc1_mip2);
    raw_read("bc7 mip 0", bc7_tex, gfx::Region {}, bc7_mip0);
    raw_read("bc7 edge block", bc7_tex, gfx::Region { mip: 1, x: 4, w: 2, h: 4 }, bc7_edge);
    raw_read("bc7 layer 1", layers, gfx::Region { layer: 1 }, bc7_layer1);
    gfx::close();
}
