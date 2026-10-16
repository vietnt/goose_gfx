// Stencil: a pass's stencil clear, the reference value, compares, every
// operation, read and write masks, the depth-fail operation, separate
// back-face operations, and stencil kept across passes that load it. A
// 4 x 1 target's pixels each go through their own steps, scissored to
// them; the stencil is then read back exactly by drawing value v over the
// pixels whose stencil equals v, for every v.
import gfx from "../..";
import std;
import os;

struct Draw { z: f32, flip: f32, v: f32, pad 4 }
struct Step { x: i64, w: i64, ref: i64, flip: bool, desc: gfx::PipelineDesc }

fn stencil(compare: i32, fail: i32, pass: i32) -> gfx::PipelineDesc {
    return gfx::PipelineDesc { write_mask: 0, stencil_test: true, stencil_compare: compare,
                               stencil_fail: fail, stencil_pass: pass };
}

fn main() {
    guard gfx::open_headless(4, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    // A triangle over the whole target at depth z, wound the other way
    // round with flip.
    let vs = embed_shader("vert", """
        layout(set = 1, binding = 0) uniform Draw { float z; float flip; float v; };
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
            gl_Position = vec4(flip > 0.5 ? -p.x : p.x, p.y, z, 1.0);
        }
        """);
    let fs = embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 3, binding = 0) uniform Draw { float z; float flip; float v; };
        void main() { c = vec4(v / 255.0, 0.0, 0.0, 1.0); }
        """);
    // The steps draw into the depth target alone: no color output.
    let depth_only = embed_shader("frag", "void main() {}");
    let color = gfx::render_target(4, 1, gfx::RGBA8);
    let ds = gfx::texture2d(4, 1, gfx::DEPTH_STENCIL, gfx::DEPTH_TARGET);
    let always = gfx::COMPARE_ALWAYS;
    let keep = gfx::STENCIL_KEEP;
    var masked = stencil(always, keep, gfx::STENCIL_REPLACE);
    masked.stencil_write_mask = 0x0C;
    var read_masked = stencil(gfx::COMPARE_EQUAL, keep, gfx::STENCIL_INVERT);
    read_masked.stencil_read_mask = 0x05;
    var depth_fails = stencil(always, keep, gfx::STENCIL_ZERO);
    depth_fails.depth_test = true;
    depth_fails.stencil_depth_fail = gfx::STENCIL_REPLACE;
    var two_sided = stencil(always, keep, gfx::STENCIL_ZERO);
    two_sided.stencil_back_compare = always;
    two_sided.stencil_back_pass = gfx::STENCIL_INCREMENT;
    let steps: Step[>..] = [
        // Cleared to 3; depth cleared to 0.25.
        Step { x: 1, w: 3, ref: 9, flip: false, desc: stencil(always, keep, gfx::STENCIL_REPLACE) },
        // The reference is on the left of the compare: 9 >= 9 passes, to
        // 10, then 9 < 10 passes, to 11 (10 < 9 would not).
        Step { x: 2, w: 2, ref: 9, flip: false, desc: stencil(gfx::COMPARE_GREATER_EQUAL, keep, gfx::STENCIL_INCREMENT_WRAP) },
        Step { x: 2, w: 2, ref: 9, flip: false, desc: stencil(gfx::COMPARE_LESS, keep, gfx::STENCIL_INCREMENT_WRAP) },
        // At pixel 3, 11 is not unequal to 11: the fail operation zeroes it,
        // which then wraps down to 255 and stays there incremented.
        Step { x: 3, w: 1, ref: 11, flip: false, desc: stencil(gfx::COMPARE_NOT_EQUAL, gfx::STENCIL_ZERO, keep) },
        Step { x: 3, w: 1, ref: 0, flip: false, desc: stencil(always, keep, gfx::STENCIL_DECREMENT) },
        Step { x: 3, w: 1, ref: 0, flip: false, desc: stencil(always, keep, gfx::STENCIL_DECREMENT_WRAP) },
        Step { x: 3, w: 1, ref: 0, flip: false, desc: stencil(always, keep, gfx::STENCIL_INCREMENT) },
        // Pixel 1: 9 with only bits 2 and 3 replaced by 255's: 13.
        Step { x: 1, w: 1, ref: 255, flip: false, desc: masked },
        // Pixels 1 and 2 against 0x15 through read mask 5: 13 & 5 is 5,
        // inverted to 242; 11 & 5 is 1, kept.
        Step { x: 1, w: 2, ref: 0x15, flip: false, desc: read_masked },
        // Pixel 2 at depth 0.5 fails LESS against 0.25: replaced by 77.
        Step { x: 2, w: 1, ref: 77, flip: false, desc: depth_fails },
        // Pixel 0: a back face, incremented from 3 to 4.
        Step { x: 0, w: 1, ref: 0, flip: true, desc: two_sided },
    ];
    // Each step in a pass of its own, loading the stencil and depth.
    var first = true;
    for s in steps {
        let p = gfx::pipeline(vs, depth_only, s.desc);
        gfx::begin_pass(gfx::PassDesc { depth: ds, clear_color: false, clear_depth: first,
                                        depth_value: 0.25, clear_stencil: first,
                                        stencil_value: 3 });
        gfx::scissor(s.x, 0, s.w, 1);
        gfx::bind(p);
        gfx::stencil_ref(s.ref);
        let u = Draw { z: 0.5, flip: if s.flip { 1.0 } else { 0.0 }, v: 0.0 };
        gfx::uniforms(gfx::VERTEX, 0, u);
        gfx::draw(3);
        gfx::end_pass();
        gfx::release(p);
        first = false;
    }

    let probe = gfx::pipeline(vs, fs, gfx::PipelineDesc { stencil_test: true,
                                                         stencil_compare: gfx::COMPARE_EQUAL });
    var d = gfx::PassDesc { depth: ds, clear_depth: false, clear_stencil: false,
                            color_value: float4 { 0.0, 0.0, 0.0, 0.0 } };
    d.color[0] = color;
    gfx::begin_pass(d);
    gfx::bind(probe);
    for v in 256 {
        gfx::stencil_ref(v);
        let u = Draw { z: 0.5, flip: 0.0, v: v };
        gfx::uniforms(gfx::VERTEX, 0, u);
        gfx::uniforms(gfx::FRAGMENT, 0, u);
        gfx::draw(3);
    }
    gfx::end_pass();
    let px = gfx::read_pixels(color);
    var got: u8[>..] = [];
    for x in 4 {
        if x > 0 { format(got, " "); }
        if px[x * 4 + 3] == 0 { format(got, "none"); } else { format(got, px[x * 4]); }
    }
    print("stencil: ", got);
    gfx::close();
}
