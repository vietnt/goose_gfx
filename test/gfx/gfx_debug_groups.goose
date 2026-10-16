// Debug groups, labels and names leave what is drawn as it is. A group
// pushed outside a pass stays open across a read back, which submits the
// command buffer it was pushed in, and across frame(): each command buffer
// gets it pushed again at its start and popped at its end, so a backend
// that wants groups balanced in a command buffer (Metal) gets them so.
// Groups pushed in a pass are popped in it, render and compute.
import gfx from "../..";
import std;
import os;

struct Color { c: float4 }

fn main() {
    guard gfx::open_headless(2, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let fill = gfx::pipeline(embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """), embed_shader("frag", """
        layout(location = 0) out vec4 c;
        layout(set = 3, binding = 0) uniform Color { vec4 color; };
        void main() { c = color; }
        """));
    let count = gfx::compute_pipeline(embed_shader("comp", """
        layout(local_size_x = 4) in;
        layout(std430, set = 1, binding = 0) buffer Out { uint o[]; };
        void main() { o[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 3u; }
        """));
    let target = gfx::render_target(2, 1, gfx::RGBA8);
    gfx::set_name(target, "debug groups target");
    let out = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 16, "");
    gfx::set_name(out, "debug groups buffer");

    gfx::push_group("outer, across a read back and a frame");
    gfx::label("before the first pass");
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::push_group("first pass");
    gfx::bind(fill);
    gfx::label("red");
    gfx::uniforms(gfx::FRAGMENT, 0, Color { c: float4 { 1.0, 0.0, 0.0, 1.0 } });
    gfx::draw(3);
    gfx::pop_group();
    gfx::end_pass();
    let first = gfx::read_pixels(target);
    print("first pass: ", first[0], ",", first[1], ",", first[2], ",", first[3]);

    let bufs: gfx::Buffer[1] = [out];
    gfx::begin_compute(bufs);
    gfx::push_group("compute");
    gfx::bind(count);
    gfx::dispatch(1, 1, 1);
    gfx::pop_group();
    gfx::end_compute();
    gfx::frame();

    gfx::push_group("inner, after the frame");
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 1.0, 1.0 });
    gfx::bind(fill);
    gfx::uniforms(gfx::FRAGMENT, 0, Color { c: float4 { 0.0, 1.0, 0.0, 1.0 } });
    gfx::scissor(1, 0, 1, 1);
    gfx::draw(3);
    gfx::end_pass();
    gfx::pop_group();
    gfx::pop_group();
    let second = gfx::read_pixels(target);
    print("second pass: ", second[0], ",", second[1], ",", second[2], ",", second[3], "  ",
          second[4], ",", second[5], ",", second[6], ",", second[7]);
    var counted: u32[>..] = [];
    gfx::read_buffer(out, counted, 4);
    print("compute: ", counted);
    gfx::close();
}
