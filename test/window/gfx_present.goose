// The present mode a window gets from each open() flag, which no headless
// test can see. Needs a display, as the rest of test/window; on Linux with
// none, under Xvfb (docs/gfx.md, Testing). Lavapipe prints:
//   no flag: present mode 0       (PRESENT_VSYNC)
//   NO_VSYNC: present mode 2      (PRESENT_MAILBOX)
//   IMMEDIATE: present mode 1     (PRESENT_IMMEDIATE)
// A window without mailbox or immediate keeps the other, or vsync.
import gfx from "../..";
import std;
import os;

fn main() {
    let names: const u8[:][3] = ["no flag", "NO_VSYNC", "IMMEDIATE"];
    let flags: i32[3] = [0, gfx::NO_VSYNC, gfx::IMMEDIATE];
    for i in 3 {
        guard gfx::open("gfx_present", 64, 64, flags[i] | gfx::WINDOW_HIDDEN) else {
            print(names[i], ": no window: ", gfx::error());
            return;
        }
        gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
        gfx::end_pass();
        gfx::frame();
        print(names[i], ": present mode ", gfx::present_mode());
        gfx::close();
    }
}
