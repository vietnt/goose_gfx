// Uploads are staged in a ring of 2 buffers, so a program that uploads
// 300 MB before its first frame, as a game loading its world does, holds
// at most those 2 buffers of staging, not a copy of all 300 MB until the
// frame is submitted. A 4 MB texture is updated 75 times; each update marks
// its first texel, and the last one's mark is what reads back. Where the
// process's memory can be read, it grows by no more than the 2 buffers and
// the texture (24 MB is allowed for that), where holding every update's
// staging would grow it by 300 MB.
import gfx from "../..";
import std;
import os;

// The process's resident memory in MB, or -1 where it can't be read: there
// that check checks nothing.
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
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let t = gfx::texture2d(1024, 1024);
    var bytes: u8[>..] = [];
    for i in 1024 * 1024 * 4 { bytes.push((i * 7 + 3) as u8); }
    let before = resident_mb();
    for k in 75 {
        bytes[0] = k as u8;
        gfx::update_texture(t, bytes);
    }
    let grown = resident_mb() - before;
    let info = gfx::upload_info();
    let allowed = 2 * (info.capacity >> 20) + 24;
    gfx::frame();
    let px = gfx::read_pixels(t);
    let mb = info.capacity >> 20;
    print("ring buffers of 64 or 128 MB: ", info.buffers == 2 && (mb == 64 || mb == 128));
    print("staging held more than one buffer and at most both: ",
          info.peak > info.capacity && info.peak <= 2 * info.capacity);
    print("memory grew by no more than the ring's 2 buffers: ", before < 0 || grown <= allowed);
    if before >= 0 && grown > allowed { print("it grew by ", grown, " MB, more than ", allowed); }
    print("the last update arrived: ", px[0] == 74 && px[1] == 10 && px[4194303] == ((4194303 * 7 + 3) & 255));
    gfx::check();
    gfx::close();
}
