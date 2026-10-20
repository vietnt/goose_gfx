// The upload ring shrinks once uploads are light, grows back for a load,
// and lets its buffers go once uploads stop. A load of 192 MB fills both
// full-size buffers; then a 256 KB field is updated every frame, which is
// light. 29 frames on the buffers are still full size, and the 30th
// releases them for 8 MB ones. Where the process's memory can be read, it
// falls by most of the load's staging, and stays flat over 160 more frames
// of the field, where full-size buffers kept and walked through by its
// updates grew it by 31 MB on Lavapipe. A 64 MB load after that goes back to a full-size buffer and
// arrives whole, and 30 frames with no upload at all release it.
import gfx from "../..";
import std;
import os;

// The process's resident memory in MB, or -1 where it can't be read: there
// the memory checks check nothing.
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

fn frames(n: i64) {
    for i in n { gfx::frame(); }
}

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::frame();
    let cap = gfx::upload_info().capacity;
    let big = gfx::texture2d(4096, 4096);
    var bytes: u8[>..] = [];
    bytes.resize(4096 * 4096 * 4, 0);
    for i in bytes.len { bytes[i] = (i * 5 + 1) as u8; }
    for k in 3 { gfx::update_texture(big, bytes); }
    gfx::frame();
    print("the load made both full-size buffers: ", gfx::upload_info().allocated == 2 * cap);

    let field = gfx::texture2d(512, 128);
    var fb: u8[>..] = [];
    fb.resize(512 * 128 * 4, 9);
    var tick = 0;
    fn update_field() {
        fb[0] = (tick & 255) as u8;
        tick++;
        gfx::update_texture(field, fb);
        gfx::frame();
    }
    for i in 29 { update_field(); }
    print("29 light frames on, still full size: ", gfx::upload_info().allocated == 2 * cap);
    let before = resident_mb();
    update_field();
    for i in 10 { update_field(); }
    let small = gfx::upload_info().allocated;
    print("the 30th shrinks them to 8 MB buffers: ", small > 0 && small <= 16 * 1048576);
    let shrunk = resident_mb();
    print("memory fell by most of the load's staging: ", before < 0 || before - shrunk >= 96);
    if before >= 0 && before - shrunk < 96 { print("it fell by ", before - shrunk, " MB"); }
    for i in 160 { update_field(); }
    let later = resident_mb() - shrunk;
    print("160 more frames of the field, memory flat: ", before < 0 || later < 4);
    if before >= 0 && later >= 4 { print("it grew by ", later, " MB"); }
    let px = gfx::read_pixels(field);
    print("the last field update arrived: ", px[0] == ((tick - 1) & 255) && px[1] == 9);

    for i in bytes.len { bytes[i] = (i * 3 + 2) as u8; }
    gfx::update_texture(big, bytes);
    print("a 64 MB load goes back to a full-size buffer: ", gfx::upload_info().allocated == cap);
    gfx::frame();
    let back = gfx::read_pixels(big);
    var off = 0;
    for i in bytes.len { if back[i] != bytes[i] { off++; } }
    print("its bytes off: ", off);
    frames(29);
    print("29 frames with no upload, buffers: ", gfx::upload_info().allocated / cap);
    gfx::frame();
    print("the 30th releases them, buffers: ", gfx::upload_info().allocated / cap);
    gfx::check();
    gfx::close();
}
