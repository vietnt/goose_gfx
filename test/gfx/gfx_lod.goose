// Which mip level sampling reaches: by the implicit level of detail, by an
// explicit one, by explicit gradients, and as a sampler's min_lod and
// max_lod clamp them. Every mip of every layer is one flat color, so the
// level a pixel read is exact in what it reads back.
import gfx from "../..";
import std;
import os;

struct Lod { lod: f32, layer: f32, pad 8 }
struct Case { name: const u8[:], p: gfx::Pipeline, t: gfx::Texture, s: gfx::SamplerDesc,
              target: gfx::Texture, lod: f32, layer: i64 }

// The color of mip `m` of layer `l`.
fn color(m: i64, l: i64) -> u8[4] {
    return [(m * 60 + 10) as u8, (l * 80 + 20) as u8, (200 - m * 40) as u8, 255];
}

fn level_of(px: u8[:], l: i64) -> u8[>..] {
    var out: u8[>..] = [];
    // Every pixel of the target must agree.
    for i in px.len / 4 {
        for c in 4 {
            if px[i * 4 + c] != px[c] { format(out, "pixels differ"); return out; }
        }
    }
    for m in 4 {
        let c = color(m, l);
        if px[0] == c[0] && px[1] == c[1] && px[2] == c[2] { format(out, "mip ", m); return out; }
    }
    format(out, "no mip: ", px[0], ",", px[1], ",", px[2], ",", px[3]);
    return out;
}

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    // 8 x 8 with 4 mips, and an array of 3 layers of the same.
    let flat = gfx::texture(gfx::TextureDesc { width: 8, height: 8, mips: 4 });
    let layered = gfx::texture(gfx::TextureDesc { kind: gfx::TEXTURE_2D_ARRAY, width: 8, height: 8,
                                                  depth: 3, mips: 4 });
    for m in 4 {
        let n = 8 >> m;
        for l in 3 {
            var px: u8[>..] = [];
            for i in n * n { px.append(color(m, l)); }
            if l == 0 { gfx::update_texture(flat, gfx::Region { mip: m as i32 }, px); }
            gfx::update_texture(layered, gfx::Region { mip: m as i32, layer: l as i32 }, px);
        }
    }
    // Texture coordinates 0 to 1 over the target: an 8 x 8 target reads the
    // texture 1:1 (level 0), a 2 x 2 one at 4 texels a pixel (level 2).
    let vs = embed_shader("vert", """
        layout(location = 0) out vec2 v_uv;
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            v_uv = p;
            gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
        }
        """);
    let implicit = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2D t;
        void main() { c = texture(t, v_uv); }
        """));
    let explicit = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2D t;
        layout(set = 3, binding = 0) uniform Lod { float lod; float layer; };
        void main() { c = textureLod(t, v_uv, lod); }
        """));
    // Gradients of half the texture a pixel: 4 texels of 8, level 2, where
    // the implicit derivatives of an 8 x 8 target give level 0.
    let grad = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2DArray t;
        layout(set = 3, binding = 0) uniform Lod { float lod; float layer; };
        void main() { c = textureGrad(t, vec3(v_uv, layer), vec2(0.5, 0.0), vec2(0.0, 0.5)); }
        """));
    let implicit_array = gfx::pipeline(vs, embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 c;
        layout(set = 2, binding = 0) uniform sampler2DArray t;
        layout(set = 3, binding = 0) uniform Lod { float lod; float layer; };
        void main() { c = texture(t, vec3(v_uv, layer)); }
        """));
    let whole = gfx::SamplerDesc { min_filter: gfx::NEAREST, mag_filter: gfx::NEAREST,
                                   mip_filter: gfx::NEAREST, wrap_u: gfx::CLAMP, wrap_v: gfx::CLAMP };
    var top = whole;
    top.max_lod = 0.0;
    var low = whole;
    low.min_lod = 2.0;
    var one = whole;
    one.min_lod = 1.0;
    one.max_lod = 1.0;
    let big = gfx::render_target(8, 8, gfx::RGBA8);
    let small = gfx::render_target(2, 2, gfx::RGBA8);

    let cases: Case[>..] = [
        Case { name: "texture() at 4:1, default range", p: implicit, t: flat, s: whole, target: small, lod: 0.0, layer: 0 },
        Case { name: "texture() at 1:1, default range", p: implicit, t: flat, s: whole, target: big, lod: 0.0, layer: 0 },
        Case { name: "textureLod(2), default range", p: explicit, t: flat, s: whole, target: big, lod: 2.0, layer: 0 },
        Case { name: "textureLod(2), max_lod 0", p: explicit, t: flat, s: top, target: big, lod: 2.0, layer: 0 },
        Case { name: "texture() at 4:1, max_lod 0", p: implicit, t: flat, s: top, target: small, lod: 0.0, layer: 0 },
        Case { name: "textureLod(0), min_lod 2", p: explicit, t: flat, s: low, target: big, lod: 0.0, layer: 0 },
        Case { name: "texture() at 4:1, lod 1 to 1", p: implicit, t: flat, s: one, target: small, lod: 0.0, layer: 0 },
        Case { name: "textureGrad 4 texels at 1:1, array layer 2", p: grad, t: layered, s: whole, target: big, lod: 0.0, layer: 2 },
        Case { name: "textureGrad 4 texels at 1:1, layer 1, max_lod 1", p: grad, t: layered, s: one, target: big, lod: 0.0, layer: 1 },
        Case { name: "texture() at 1:1, array layer 1", p: implicit_array, t: layered, s: whole, target: big, lod: 0.0, layer: 1 },
    ];
    for c in cases {
        gfx::begin_pass(c.target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
        gfx::bind(c.p);
        gfx::bind_texture(gfx::FRAGMENT, 0, c.t, gfx::sampler(c.s));
        if c.p.id != implicit.id {
            gfx::uniforms(gfx::FRAGMENT, 0, Lod { lod: c.lod, layer: c.layer as f32 });
        }
        gfx::draw(3);
        gfx::end_pass();
        print(c.name, ": ", level_of(gfx::read_pixels(c.target), c.layer));
    }
    gfx::close();
}
