// frame() keeps at most two frames on the GPU: with two still running it
// waits for the older before submitting another, headless too. Every frame
// here draws a slow pass, and a readback asked for in a frame is ready once
// the frame two after it has been submitted. How many frames are still
// running at a submit depends on how fast the GPU is, so only the bound is
// checked.
// Frames paced this way also stay within bounded memory, frames that clear
// a depth target many times over included, each clear moving it to fresh
// memory while the GPU may still use the old.
// And with every frame's work at least 30 ms of the GPU's, so that the GPU
// is what holds frames back, each steady frame() leaves 2 frames in flight:
// nothing else frame() does may wait for the GPU to go idle, as reading
// command buffer timings does on Lavapipe, which leaves frames coming in
// bunches with the GPU drained between them. (How long one frame() takes
// is no measure of this: a wait that wakes late leaves the next with
// nothing to wait for.)
import gfx from "../..";
import std;
import os;

struct Work { iterations: i32, pad 12 }

// The process's resident memory in MB, or -1 where it can't be read: there
// the check below checks nothing.
fn resident_mb() -> i64 {
    var s: u8[>..] = [];
    if !read_file("/proc/self/statm", s) { return -1; }
    var i = 0;
    while i < s.len && s[i] != 32 { i++; }
    var pages = 0;
    i++;
    while i < s.len && s[i] >= 48 && s[i] <= 57 { pages = pages * 10 + (s[i] - 48); i++; }
    return pages * 4096 / 1048576;
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
    let slow = gfx::pipeline(vs, embed_shader("frag", """
        layout(set = 3, binding = 0) uniform Work { int iterations; };
        layout(location = 0) out vec4 c;
        void main() {
            float x = gl_FragCoord.x * 0.001;
            for (int i = 0; i < iterations; i++) x = fract(x * 1.618 + 0.25);
            c = vec4(x, 0.0, 0.0, 1.0);
        }
        """));
    let target = gfx::render_target(256, 256, gfx::RGBA8);
    let marker = gfx::render_target(1, 1, gfx::RGBA8);
    var reads: gfx::Readback[8] = [gfx::Readback { 0 }; 8];
    var most = 0;
    var ready_two_on = 0;
    var frame = 0;
    while gfx::frame() && frame < 10 {
        if frame >= 2 {
            most = max(most, gfx::frames_in_flight());
            // Frame - 3 had to finish before frame - 1 was submitted.
            if frame - 3 >= 0 && frame - 3 < 8 && gfx::ready(reads[frame - 3]) { ready_two_on++; }
        }
        gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 1.0 });
        gfx::bind(slow);
        gfx::uniforms(gfx::FRAGMENT, 0, Work { iterations: 4000 });
        gfx::draw(3);
        gfx::end_pass();
        if frame < 8 {
            gfx::begin_pass(marker, gfx::Texture { 0 }, float4 { (frame * 32) / 255.0, 0.0, 0.0, 1.0 });
            gfx::end_pass();
            reads[frame] = gfx::read_texture_async(marker);
        }
        frame++;
    }
    print("never more than 2 frames in flight at a submit: ", most <= 2);
    print("readbacks ready once the frame two after theirs was submitted: ", ready_two_on, " of 7");
    for i in 8 {
        let px = gfx::take(reads[i]);
        if px[0] != (i * 32) as u8 { print("frame ", i, " read ", px[0]); }
    }

    let depth = gfx::depth_target(256, 256);
    var pass = gfx::PassDesc { depth: depth };
    pass.color[0] = target;
    var before = 0;
    for n in 120 {
        if n == 20 { before = resident_mb(); }
        gfx::frame();
        for k in 20 {
            gfx::begin_pass(pass);
            gfx::end_pass();
        }
    }
    let grown = resident_mb() - before;
    print("memory grew by under 16 MB over 100 frames of 20 depth clears: ", before < 0 || grown < 16);
    if grown >= 16 { print("it grew by ", grown, " MB"); }

    var iterations = 1000;
    var work = 0.0;
    while work < 0.03 && iterations < 1 << 22 {
        iterations *= 2;
        let t0 = gfx::time();
        gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 1.0 });
        gfx::bind(slow);
        gfx::uniforms(gfx::FRAGMENT, 0, Work { iterations: iterations as i32 });
        gfx::draw(3);
        gfx::end_pass();
        gfx::flush();
        work = gfx::time() - t0;
    }
    var drained = 0;
    for n in 14 {
        gfx::frame();
        if n >= 4 && gfx::frames_in_flight() < 2 { drained++; }
        gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 1.0 });
        gfx::bind(slow);
        gfx::uniforms(gfx::FRAGMENT, 0, Work { iterations: iterations as i32 });
        gfx::draw(3);
        gfx::end_pass();
    }
    print("steady frames that left fewer than 2 frames in flight: ", drained, " of 10");
    gfx::close();
}
