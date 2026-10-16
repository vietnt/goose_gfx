// Blend factors, operations, the blend constant and write masks: one
// source color drawn over one cleared destination, in each way, into an
// RGBA8 target read back exactly. Every result is a sum or difference of
// whole channel values, or a half or quarter of an even one, so no
// rounding comes into it; the source and destination differ in every
// channel, and the color and alpha parts of each case differ, so a wrong
// operation, a swapped factor or alpha blended as color shows.
import gfx from "../..";
import std;
import os;

struct Color { c: float4 }
struct Case { name: const u8[:], constant: bool, k: float4, desc: gfx::PipelineDesc }

let src = float4 { 60.0 / 255.0, 180.0 / 255.0, 20.0 / 255.0, 100.0 / 255.0 };
let dst = float4 { 200.0 / 255.0, 100.0 / 255.0, 40.0 / 255.0, 160.0 / 255.0 };

fn custom(s: i32, d: i32, op: i32, sa: i32, da: i32, opa: i32) -> gfx::PipelineDesc {
    return gfx::PipelineDesc { blend: gfx::BLEND_CUSTOM, blend_src: s, blend_dst: d, blend_op: op,
                               blend_src_alpha: sa, blend_dst_alpha: da, blend_op_alpha: opa };
}

fn main() {
    guard gfx::open_headless(1, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let vs = embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """);
    let fs = embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 3, binding = 0) uniform Color { vec4 color; };
        void main() { c = color; }
        """);
    let one = gfx::FACTOR_ONE;
    let zero = gfx::FACTOR_ZERO;
    let none = float4 { 0.0, 0.0, 0.0, 0.0 };
    let cases: Case[>..] = [
        Case { name: "src - dst, alpha src", constant: false, k: none,
               desc: custom(one, one, gfx::OP_SUBTRACT, one, zero, gfx::OP_ADD) },
        Case { name: "dst - src, alpha max", constant: false, k: none,
               desc: custom(one, one, gfx::OP_REVERSE_SUBTRACT, one, one, gfx::OP_MAX) },
        Case { name: "min, alpha dst", constant: false, k: none,
               desc: custom(one, one, gfx::OP_MIN, zero, one, gfx::OP_ADD) },
        Case { name: "constant 0.5,0.25,0,1 and one minus it", constant: true,
               k: float4 { 0.5, 0.25, 0.0, 1.0 },
               desc: custom(gfx::FACTOR_CONSTANT, gfx::FACTOR_ONE_MINUS_CONSTANT, gfx::OP_ADD,
                            gfx::FACTOR_CONSTANT, gfx::FACTOR_ONE_MINUS_CONSTANT, gfx::OP_ADD) },
        Case { name: "constant as the pass starts", constant: false, k: none,
               desc: custom(gfx::FACTOR_CONSTANT, zero, gfx::OP_ADD, gfx::FACTOR_CONSTANT, zero,
                            gfx::OP_ADD) },
        Case { name: "write mask r and a", constant: false, k: none,
               desc: gfx::PipelineDesc { write_mask: gfx::WRITE_R | gfx::WRITE_A } },
        Case { name: "write mask g, blending add", constant: false, k: none,
               desc: gfx::PipelineDesc { blend: gfx::BLEND_CUSTOM, blend_dst: one,
                                         write_mask: gfx::WRITE_G } },
        Case { name: "write mask none", constant: false, k: none,
               desc: gfx::PipelineDesc { write_mask: 0 } },
    ];
    let target = gfx::render_target(1, 1, gfx::RGBA8);
    print("src 60,180,20,100 over dst 200,100,40,160");
    for c in cases {
        let p = gfx::pipeline(vs, fs, c.desc);
        gfx::begin_pass(target, gfx::Texture { 0 }, dst);
        gfx::bind(p);
        if c.constant { gfx::blend_constant(c.k); }
        gfx::uniforms(gfx::FRAGMENT, 0, Color { c: src });
        gfx::draw(3);
        gfx::end_pass();
        let px = gfx::read_pixels(target);
        print(c.name, ": ", px[0], ",", px[1], ",", px[2], ",", px[3]);
        gfx::release(p);
    }
    gfx::close();
}
