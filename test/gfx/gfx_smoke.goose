// The suite's own check that it runs this checkout's gfx: a target cleared
// and drawn into, read back exactly.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let target = gfx::render_target(2, 1, gfx::RGBA8);
    // The left pixel only: x from -1 to 0 in clip space.
    let vs = embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p.x - 1.0, p.y * 2.0 - 1.0, 0.0, 1.0);
        }
        """);
    let fs = embed_shader("frag", """
        layout(location = 0) out vec4 c;
        void main() { c = vec4(0.2, 0.4, 1.0, 1.0); }
        """);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 1.0, 0.0, 0.2, 1.0 });
    gfx::bind(gfx::pipeline(vs, fs));
    gfx::draw(3);
    gfx::end_pass();
    let px = gfx::read_pixels(target);
    print("drawn, cleared: ", px[0], ",", px[1], ",", px[2], ",", px[3], " ",
          px[4], ",", px[5], ",", px[6], ",", px[7]);
    gfx::close();
}
