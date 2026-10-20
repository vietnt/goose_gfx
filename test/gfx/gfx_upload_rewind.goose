// Uploads made every frame reuse the start of the upload ring's buffers.
// A 4 MB texture is updated in each of 100 frames, 400 MB in all, more than
// the two buffers hold: each frame's update goes to the start of a buffer
// the GPU is done with, so the ring never holds more than a few frames'
// updates at once, where appending would walk through both buffers and
// hold all of them. Each update is marked, and the last one reads back.
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::frame();
    let t = gfx::texture2d(1024, 1024);
    var bytes: u8[>..] = [];
    for i in 1024 * 1024 * 4 { bytes.push((i * 7 + 3) as u8); }
    for k in 100 {
        bytes[0] = k as u8;
        gfx::update_texture(t, bytes);
        gfx::frame();
    }
    let info = gfx::upload_info();
    print("100 updates of 4 MB, more than the ring holds: ", 100 * bytes.len > 2 * info.capacity);
    print("the ring held at most 4 frames' updates at once: ", info.peak <= 4 * bytes.len + 4096);
    if info.peak > 4 * bytes.len + 4096 { print("it held ", info.peak >> 20, " MB"); }
    let px = gfx::read_pixels(t);
    print("the last update arrived: ", px[0] == 99 && px[1] == 10);
    gfx::check();
    gfx::close();
}
