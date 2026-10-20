// The upload ring lets its buffers go once uploads stop, and makes them
// again for the next one. A 64 MB texture is uploaded, then a 1 x 1 one 60
// frames later, which starts the count again: 119 frames after that the
// buffer is still there, and the 120th frame releases it. Where the
// process's memory can be read, it falls by most of the 64 MB the buffer
// held. A later upload of new contents makes a buffer again and arrives
// whole.
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

fn frames(n: i64) {
    for i in n { gfx::frame(); }
}

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::frame();
    let big = gfx::texture2d(4096, 4096);
    var bytes: u8[>..] = [];
    bytes.resize(4096 * 4096 * 4, 0);
    for i in bytes.len { bytes[i] = (i * 5 + 1) as u8; }
    gfx::update_texture(big, bytes);
    gfx::frame();
    let size = gfx::upload_info().buffer_bytes;
    print("buffers made by the upload: ", gfx::upload_info().allocated / size);
    frames(59);
    let small = gfx::texture2d(1, 1);
    let one: u8[4] = [1, 2, 3, 4];
    gfx::update_texture(small, one);
    gfx::frame();
    frames(119);
    print("119 frames after the last upload, buffers: ", gfx::upload_info().allocated / size);
    let before = resident_mb();
    gfx::frame();
    print("the 120th releases them, buffers: ", gfx::upload_info().allocated / size);
    frames(3);
    let freed = before - resident_mb();
    print("memory fell by most of the buffer's 64 MB: ", before < 0 || freed >= 48);
    if before >= 0 && freed < 48 { print("it fell by ", freed, " MB"); }

    for i in bytes.len { bytes[i] = (i * 3 + 2) as u8; }
    gfx::update_texture(big, bytes);
    print("a later upload makes a buffer again, buffers: ", gfx::upload_info().allocated / size);
    gfx::frame();
    let px = gfx::read_pixels(big);
    var off = 0;
    for i in bytes.len { if px[i] != bytes[i] { off++; } }
    print("its bytes off: ", off);
    gfx::check();
    gfx::close();
}
