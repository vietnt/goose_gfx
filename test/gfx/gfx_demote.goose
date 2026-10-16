// needs: demote
// A fragment shader's demote: the pixel it runs for keeps the target's
// clear color, but the invocation goes on as a helper, so the derivatives
// its quad's other pixels take after it are those of every pixel's own
// values. One pixel of each 2 x 2 quad demotes; the rest write 10 times
// the derivatives of x * x and y * y, which only the demoted pixel's own
// x and y make 20 for the quad at 0 and 60 for the one at 2. (Lavapipe
// runs a discarded invocation on too, so there discard gives the same.)
import gfx from "../..";
import std;
import os;

fn main() {
    guard gfx::open_headless(4, 4) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let p = gfx::pipeline(embed_shader("vert", """
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
        """), embed_shader("frag", """
        layout(location = 0) out vec4 c;
        void main() {
            vec2 at = gl_FragCoord.xy;
            if (mod(floor(at.x), 2.0) == 0.0 && mod(floor(at.y), 2.0) == 0.0) demote;
            vec2 sq = at * at;
            c = vec4(dFdx(sq.x) * 10.0 / 255.0, dFdy(sq.y) * 10.0 / 255.0, 0.0, 1.0);
        }
        """));
    let target = gfx::render_target(4, 4, gfx::RGBA8);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 1.0, 0.0 });
    gfx::bind(p);
    gfx::draw(3);
    gfx::end_pass();
    let px = gfx::read_pixels(target);
    for y in 4 {
        var row: u8[>..] = [];
        for x in 4 {
            let i = (y * 4 + x) * 4;
            format(row, if x > 0 { "  " } else { "" }, px[i], ",", px[i + 1], ",", px[i + 2], ",", px[i + 3]);
        }
        print("row ", y, ": ", row);
    }
    gfx::close();
}
