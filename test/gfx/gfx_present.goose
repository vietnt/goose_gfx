// The present flags off screen: open_headless takes IMMEDIATE and NO_VSYNC,
// and with no window there is no present mode. Which one a window gets is
// checked with a display (docs/gfx.md, Testing).
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(4, 4, gfx::IMMEDIATE | gfx::NO_VSYNC) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::end_pass();
    print("headless with IMMEDIATE and NO_VSYNC: frame ", gfx::frame(), ", present mode ", gfx::present_mode());
    gfx::close();
}
