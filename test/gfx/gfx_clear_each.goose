// A pass with clear_each clears each color target to its own value, the
// float target too; one without it clears all of them to color_value, and
// ignores color_values. Every channel of every target differs, so a value
// taken from the wrong slot or the wrong field shows.
import gfx from "../..";
import std;
import os;

fn bytes(name: string, t: gfx::Texture) {
    let px = gfx::read_pixels(t);
    print(name, ": ", px[0], ",", px[1], ",", px[2], ",", px[3], "  ", px[4], ",", px[5], ",",
          px[6], ",", px[7]);
}

fn floats(name: string, t: gfx::Texture) {
    var px: float4[>..] = [];
    gfx::read_texture(t, px);
    print(name, ": ", px[0].x, " ", px[0].y, " ", px[0].z, " ", px[0].w, "  ", px[1].x, " ",
          px[1].y, " ", px[1].z, " ", px[1].w);
}

fn main() {
    guard gfx::open_headless(2, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let a = gfx::render_target(2, 1, gfx::RGBA8);
    let f = gfx::render_target(2, 1, gfx::RGBA32F);
    let b = gfx::render_target(2, 1, gfx::RGBA8);

    var each = gfx::PassDesc { clear_each: true, color_value: float4 { 0.0, 1.0, 0.0, 1.0 } };
    each.color[0] = a;
    each.color[1] = f;
    each.color[2] = b;
    each.color_values[0] = float4 { 0.2, 0.4, 0.6, 0.8 };
    each.color_values[1] = float4 { -2.5, 1024.25, 3.0, 0.5 };
    each.color_values[2] = float4 { 1.0, 0.0, 0.6, 0.2 };
    gfx::begin_pass(each);
    gfx::end_pass();
    print("clear_each");
    bytes("  target 0", a);
    floats("  target 1", f);
    bytes("  target 2", b);

    var all = gfx::PassDesc { color_value: float4 { 0.0, 0.2, 0.6, 1.0 } };
    all.color[0] = a;
    all.color[1] = f;
    all.color[2] = b;
    all.color_values[0] = float4 { 1.0, 1.0, 1.0, 1.0 };
    all.color_values[2] = float4 { 1.0, 1.0, 1.0, 1.0 };
    gfx::begin_pass(all);
    gfx::end_pass();
    print("color_value");
    bytes("  target 0", a);
    floats("  target 1", f);
    bytes("  target 2", b);

    gfx::check();
    gfx::close();
}
