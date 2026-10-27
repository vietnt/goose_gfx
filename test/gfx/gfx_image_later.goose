// load_image_later decodes a WebP on a loading thread and image_later_state
// puts it into its texture: a 3 x 2 picture whose every texel differs, at 2, 2
// of a 7 x 6 texture with a border of 1, so its edge texels must reach one
// texel out on each side (the corners too) and nothing else may change. Then
// a wrong size, a missing file and a job that is not one fail, with the reason.
// image_size_later reads the size of each kind of WebP (lossless VP8L, lossy
// VP8, and VP8X: lossy with alpha) and fails on a missing file.
import gfx from "../..";
import std;
import os;

let PATH = "test/gfx/gfx_image_later.webp";

// Each texel as the picture's texel it holds (0 to 5), or . for the fill.
fn grid(px: u8[:], w: i64, h: i64) {
    for y in h {
        var line: u8[>..] = [];
        for x in w {
            let i = (y * w + x) * 4;
            var c: u8 = '.';
            for p in 6 {
                if px[i] == (10 + p * 20) as u8 && px[i + 1] == (200 - p * 20) as u8 && px[i + 2] == (7 * p + 3) as u8 && px[i + 3] == 255 { c = ('0' as i64 + p) as u8; }
            }
            if c == '.' && !(px[i] == 1 && px[i + 1] == 2 && px[i + 2] == 3 && px[i + 3] == 4) { c = '?'; }
            line.push(c);
        }
        write_stdout(str(line, "\n"));
    }
}

// The job's state once it is not 0 (at most 5 s).
fn settle(job: i64) -> i32 {
    let t0 = clock();
    while clock() - t0 < 5.0 {
        let s = gfx::image_later_state(job);
        if s != 0 { return s; }
        sleep(0.001);
    }
    0
}

fn main() {
    guard gfx::open_headless(8, 8) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let t = gfx::texture2d(7, 6);
    var fill: u8[>..] = [];
    for i in 7 * 6 { fill.push(1); fill.push(2); fill.push(3); fill.push(4); }
    gfx::update_texture(t, fill);
    let job = gfx::load_image_later(PATH, t, 2, 2, 3, 2, 1);
    write_stdout(str("queued: ", job > 0, "\n"));
    write_stdout(str("state: ", settle(job), "\n"));
    grid(gfx::read_pixels(t), 7, 6);

    let wrong = gfx::load_image_later(PATH, t, 2, 2, 4, 2, 1);
    write_stdout(str("wrong size: ", settle(wrong), ", ", ends_with(gfx::error(), "is 3 x 2, not 4 x 2"), "\n"));
    let missing = gfx::load_image_later("test/gfx/no-such-picture.webp", t, 2, 2, 3, 2, 1);
    write_stdout(str("missing: ", settle(missing), ", ", starts_with(gfx::error(), "cannot load test/gfx/no-such-picture.webp"), "\n"));
    write_stdout(str("done job: ", gfx::image_later_state(job), ", ", gfx::error(), "\n"));
    grid(gfx::read_pixels(t), 7, 6);

    let files = [PATH, "test/gfx/gfx_image_later_lossy.webp", "test/gfx/gfx_image_later_alpha.webp", "test/gfx/no-such-picture.webp"];
    var jobs: i64[>..] = [];
    for f in files { jobs.push(gfx::image_size_later(f)); }
    for i in jobs.len {
        let t0 = clock();
        var r: i64 = 0;
        while r == 0 && clock() - t0 < 5.0 { r = gfx::image_size_state(jobs[i]); if r == 0 { sleep(0.001); } }
        if r > 0 { write_stdout(str("size: ", r / 65536, " x ", r % 65536, "\n")); }
        else { write_stdout(str("size: ", r, ", ", starts_with(gfx::error(), "cannot load test/gfx/no-such-picture.webp"), "\n")); }
    }
    let picture = gfx::load_image_later(PATH, t, 2, 2, 3, 2, 1);
    let t1 = clock();
    var r: i64 = 0;
    while r == 0 && clock() - t1 < 5.0 { r = gfx::image_size_state(picture); if r == 0 { sleep(0.001); } }
    write_stdout(str("a picture's job as a size: ", r, ", ", gfx::error(), "\n"));
    gfx::close();
}
