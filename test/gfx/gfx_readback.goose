// Read backs that do not wait: each gives what the resource held where it
// was asked for, in program order, whatever is drawn or written after it;
// ready() is false until it is submitted; several share one submit and are
// taken in any order; one not submitted yet is submitted by take(); the
// source can be released before the take; and reading depth this way
// leaves the uniforms the program pushed as they were.
import gfx from "../..";
import std;
import os;

struct Color { c: float4 }
struct Rect { r: float4, z: float4 }

fn texels(px: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for i in px.len / 4 {
        if i > 0 { out.push(' '); }
        format(out, px[i * 4], ",", px[i * 4 + 1], ",", px[i * 4 + 2], ",", px[i * 4 + 3]);
    }
    return out;
}

fn clear(t: gfx::Texture, c: float4) {
    gfx::begin_pass(t, gfx::Texture { 0 }, c);
    gfx::end_pass();
}

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    // Program order: cleared to one color, asked for, cleared to another,
    // asked for again.
    let target = gfx::render_target(2, 1, gfx::RGBA8);
    clear(target, float4 { 1.0, 0.0, 0.0, 1.0 });
    let first = gfx::read_texture_async(target);
    clear(target, float4 { 0.0, 0.0, 1.0, 1.0 });
    let second = gfx::read_texture_async(target, gfx::Region { x: 1, w: 1, h: 1 });
    print("before any submit: ready ", gfx::ready(first), " ", gfx::ready(second),
          ", sizes ", gfx::readback_size(first), " ", gfx::readback_size(second));
    // Taken out of order; the second is still pending, so take submits it.
    print("second, taken first: ", texels(gfx::take(second)));
    print("first, ready after the second's take: ", gfx::ready(first));
    print("first: ", texels(gfx::take(first)));

    // A buffer: asked for, then overwritten; flush() submits it.
    let values: u32[4] = [10, 20, 30, 40];
    let buf = gfx::buffer(gfx::BUFFER_VERTEX, bytes_of(values));
    let middle = gfx::read_buffer_async(buf, 4, 8);
    let overwrite: u32[4] = [1, 2, 3, 4];
    gfx::update_buffer(buf, bytes_of(overwrite));
    print("buffer before flush: ready ", gfx::ready(middle));
    gfx::flush();
    print("buffer after flush: ready ", gfx::ready(middle));
    var got: u32[>..] = [];
    gfx::take(middle, got);
    print("buffer bytes 4 to 12 as asked for: ", got[0], " ", got[1]);
    let after = gfx::read_buffer_async(buf, 0, 16);
    var now: u32[>..] = [];
    gfx::take(after, now);
    print("buffer now: ", now[0], " ", now[1], " ", now[2], " ", now[3]);

    // Three from one submit, taken last to first, from a texture released
    // before any of them is taken.
    let doomed = gfx::render_target(3, 1, gfx::RGBA8);
    clear(doomed, float4 { 0.2, 0.4, 0.6, 1.0 });
    var three: gfx::Readback[3] = [gfx::Readback { 0 }; 3];
    for i in 3 { three[i] = gfx::read_texture_async(doomed, gfx::Region { x: i as i32, w: 1, h: 1 }); }
    gfx::release(doomed);
    gfx::frame();
    for k in 3 {
        let i = 2 - k;
        print("pixel ", i, " of a released texture: ", texels(gfx::take(three[i])));
    }
    let dropped = gfx::read_texture_async(target);
    gfx::release(dropped);

    // Depth read this way, between two draws of a pipeline whose fragment
    // uniforms are pushed only before the first: the second must still
    // have them.
    let vs = embed_shader("vert", """
        layout(set = 1, binding = 0) uniform Rect { vec4 r; vec4 z; };
        void main() {
            int i = gl_VertexIndex;
            float u = (i == 1 || i == 2 || i == 4) ? r.z : r.x;
            float v = (i == 2 || i == 4 || i == 5) ? r.w : r.y;
            gl_Position = vec4(u / z.y * 2.0 - 1.0, 1.0 - v / z.z * 2.0, z.x, 1.0);
        }
        """);
    let fs = embed_shader("frag", """
        layout(set = 3, binding = 0) uniform Color { vec4 c; };
        layout(location = 0) out vec4 result;
        void main() { result = c; }
        """);
    let draw_rect = gfx::pipeline(vs, fs, gfx::PipelineDesc { depth_test: true, depth_write: true });
    let color_a = gfx::render_target(2, 1, gfx::RGBA8);
    let color_b = gfx::render_target(2, 1, gfx::RGBA8);
    let depth = gfx::texture2d(2, 1, gfx::DEPTH32F, gfx::DEPTH_TARGET);
    var pass = gfx::PassDesc { depth: depth };
    pass.color[0] = color_a;
    gfx::begin_pass(pass);
    gfx::bind(draw_rect);
    gfx::uniforms(gfx::FRAGMENT, 0, Color { c: float4 { 0.2, 0.6, 1.0, 1.0 } });
    gfx::uniforms(gfx::VERTEX, 0, Rect { r: float4 { 0.0, 0.0, 1.0, 1.0 }, z: float4 { 0.25, 2.0, 1.0, 0.0 } });
    gfx::draw(6);
    gfx::end_pass();
    let depths = gfx::read_texture_async(depth);
    gfx::begin_pass(color_b, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(gfx::pipeline(vs, fs));
    gfx::draw(6);
    gfx::end_pass();
    var z: f32[>..] = [];
    gfx::take(depths, z);
    print("depth: ", z[0], " ", z[1]);
    print("drawn before the depth read: ", texels(gfx::read_pixels(color_a)));
    print("drawn after it, uniforms not pushed again: ", texels(gfx::read_pixels(color_b)));
    gfx::close();
}
