// frame_held()'s wait, which no headless test can see: with a window and
// no event it waits for its time, and a wake() ends a long one at once.
// Needs a display, as the rest of test/window. Every driver prints:
//   a held frame with no event waited its 0.3 s: true
//   a wake() ended a 3 s wait at once: true
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open("gfx_held", 64, 64, gfx::WINDOW_HIDDEN) else {
        print("no window: ", gfx::error());
        return;
    }
    gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::end_pass();
    gfx::frame();
    gfx::frame();
    // what the window says when it opens (shown, exposed) shows the screen once more
    gfx::frame_held(0.0);
    let t0 = gfx::time();
    gfx::frame_held(0.3);
    let waited = gfx::time() - t0;
    print("a held frame with no event waited its 0.3 s: ", waited >= 0.25 && waited < 1.0);
    if waited < 0.25 || waited >= 1.0 { print("it waited ", waited, " s"); }
    gfx::wake();
    let t1 = gfx::time();
    gfx::frame_held(3.0);
    let woken = gfx::time() - t1;
    print("a wake() ended a 3 s wait at once: ", woken < 0.5);
    if woken >= 0.5 { print("it waited ", woken, " s"); }
    gfx::close();
}
