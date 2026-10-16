// gpu_time() is -1 before any frame's timings have arrived, and once a
// finished frame's have, the seconds the GPU spent on it: positive, and for
// 64 clears of a small target well under a second. Its value differs from
// run to run, so this prints only what holds every time.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(64, 64) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    print("before any frame: ", gfx::gpu_time());
    let target = gfx::render_target(256, 256, gfx::RGBA8);
    var frames = 0;
    while frames < 30 && gfx::gpu_time() < 0.0 {
        for i in 64 {
            gfx::begin_pass(target, gfx::Texture { 0 }, float4 { (i as f32) / 64.0, 0.0, 0.0, 1.0 });
            gfx::end_pass();
        }
        gfx::frame();
        frames += 1;
    }
    let t = gfx::gpu_time();
    print("arrived within 30 frames: ", t >= 0.0);
    print("positive and under a second: ", t > 0.0 && t < 1.0);
    gfx::check();
    gfx::close();
}
