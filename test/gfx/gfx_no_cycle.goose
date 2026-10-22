// set_cycle(t, false): a colour target, a depth target and the screen,
// cleared to a new value every frame while the frames before may still be
// on the GPU reading them, keep their one copy and still hold, in program
// order, what each frame cleared and copied there. Turned back on, a target
// cycles again with the same result.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(2, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let target = gfx::render_target(2, 1, gfx::RGBA8);
    let depth = gfx::texture2d(2, 1, gfx::DEPTH32F, gfx::DEPTH_TARGET | gfx::SAMPLED);
    gfx::set_cycle(target, false);
    gfx::set_cycle(depth, false);
    gfx::set_cycle(gfx::screen(), false);
    var screens: gfx::Readback[>..] = [];
    var depths: gfx::Readback[>..] = [];
    for k in 6 {
        if k == 4 { gfx::set_cycle(target, true); }
        let v = (k + 1) as f32 / 8.0;
        var d = gfx::PassDesc { depth: depth, color_value: float4 { v, 1.0 - v, 0.0, 1.0 }, depth_value: v };
        d.color[0] = target;
        gfx::begin_pass(d);
        gfx::end_pass();
        gfx::begin_screen(float4 { 0.0, 0.0, 1.0, 1.0 });
        gfx::end_pass();
        gfx::copy_texture(target, gfx::Region { w: 1, h: 1 }, gfx::screen(), gfx::Region { x: 1 });
        screens.push(gfx::read_texture_async(gfx::screen()));
        depths.push(gfx::read_texture_async(depth));
        gfx::frame();
    }
    for k in 6 {
        let px = gfx::take(screens[k]);
        var z: f32[>..] = [];
        gfx::take(depths[k], z);
        print("frame ", k, ": screen ", px[0], ",", px[1], ",", px[2], " ", px[4], ",", px[5], ",", px[6],
              "; depth ", z[0], " ", z[1]);
    }
    gfx::check();
    gfx::close();
}
