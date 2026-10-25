// frame_held(): a frame that drew nothing new. Headless it waits for
// nothing, however long it is asked to (a held window's wait is checked in
// test/window/gfx_held.goose). It counts as a frame. A readback asked for
// in a held frame is still submitted by it (held frames after it submit
// nothing, and do not wait for the GPU). The
// screen keeps the pixels the last drawn frame left, through held frames.
// And a wake() with no window does nothing.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::frame();
    gfx::begin_screen(float4 { 1.0, 0.0, 0.0, 1.0 });
    gfx::end_pass();
    gfx::frame();
    let count = gfx::frame_count();
    let t0 = gfx::time();
    for i in 5 { gfx::frame_held(2.0); }
    print("five held frames asked to wait 2 s each took under 1 s: ", gfx::time() - t0 < 1.0);
    print("held frames counted: ", gfx::frame_count() - count);

    let marker = gfx::render_target(1, 1, gfx::RGBA8);
    gfx::begin_pass(marker, gfx::Texture { 0 }, float4 { 0.0, 1.0, 0.0, 1.0 });
    gfx::end_pass();
    let read = gfx::read_texture_async(marker);
    gfx::frame_held(0.0);
    let t1 = gfx::time();
    while !gfx::ready(read) && gfx::time() - t1 < 2.0 { gfx::frame_held(0.0); }
    print("a readback asked for in a held frame is submitted by it: ready ", gfx::ready(read));
    let px = gfx::take(read);
    print("and reads ", px[0], ",", px[1], ",", px[2], ",", px[3]);

    gfx::wake();
    gfx::frame_held(0.0);
    var screen: u8[>..] = [];
    gfx::read_texture(gfx::screen(), screen);
    print("the screen after held frames: ", screen[0], ",", screen[1], ",", screen[2], ",", screen[3]);
    gfx::close();
}
