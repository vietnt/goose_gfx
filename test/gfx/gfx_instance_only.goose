// A pipeline whose every vertex input is per instance: four instances of a
// quad made from gl_VertexIndex, each at its own cell and in its own color,
// read from vertex buffer 1 with nothing ever bound at 0. Then a pipeline
// reading vertex buffer 0, and the first again with other instances: each
// draw must find its own buffers whatever the one before it read.
import gfx from "../..";
import std;
import os;

struct Instance { x: f32, y: f32, color: u32 }

fn main() {
    guard gfx::open_headless(4, 3) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    // A 1 x 1 pixel cell for each instance: top left at (x, y) in pixels.
    let instances: Instance[4] = [
        Instance { x: 0.0, y: 0.0, color: 0xFF0000FF }, Instance { x: 1.0, y: 0.0, color: 0xFF00FF00 },
        Instance { x: 2.0, y: 0.0, color: 0xFFFF0000 }, Instance { x: 3.0, y: 0.0, color: 0xFF808080 },
    ];
    let more: Instance[4] = [
        Instance { x: 0.0, y: 2.0, color: 0xFF00FFFF }, Instance { x: 1.0, y: 2.0, color: 0xFFFFFF00 },
        Instance { x: 2.0, y: 2.0, color: 0xFFFF00FF }, Instance { x: 3.0, y: 2.0, color: 0xFF404040 },
    ];
    let per_instance = gfx::buffer(gfx::BUFFER_VERTEX, bytes_of(instances));
    let per_instance_too = gfx::buffer(gfx::BUFFER_VERTEX, bytes_of(more));
    let cells = gfx::pipeline(embed_shader("vert", """
        layout(location = 0) in vec2 a_at;
        layout(location = 1) in vec4 a_color;
        layout(location = 0) out vec4 v_color;
        void main() {
            vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
            vec2 px = a_at + corner;
            gl_Position = vec4(px.x / 2.0 - 1.0, 1.0 - px.y * 2.0 / 3.0, 0.0, 1.0);
            v_color = a_color;
        }
        """), embed_shader("frag", """
        layout(location = 0) in vec4 v_color;
        layout(location = 0) out vec4 c;
        void main() { c = v_color; }
        """), gfx::PipelineDesc { primitive: gfx::TRIANGLE_STRIP, instance_location: 0,
                                  formats: [0, gfx::VERTEX_UBYTE4_NORM, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0] });
    // The middle row from a per-vertex buffer: one triangle strip over it, white.
    let row: float2[4] = [float2 { -1.0, 0.3 }, float2 { 1.0, 0.3 }, float2 { -1.0, -0.3 }, float2 { 1.0, -0.3 }];
    let per_vertex = gfx::buffer(gfx::BUFFER_VERTEX, bytes_of(row));
    let fill = gfx::pipeline(embed_shader("vert", """
        layout(location = 0) in vec2 a_pos;
        void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
        """), embed_shader("frag", """
        layout(location = 0) out vec4 c;
        void main() { c = vec4(1.0); }
        """), gfx::PipelineDesc { primitive: gfx::TRIANGLE_STRIP });

    let target = gfx::render_target(4, 3, gfx::RGBA8);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind_vertex_buffer(1, per_instance, 0);
    gfx::bind(cells);
    gfx::draw_instanced(4, 4);
    gfx::bind_vertex_buffer(0, per_vertex, 0);
    gfx::bind(fill);
    gfx::draw(4);
    gfx::bind_vertex_buffer(1, per_instance_too, 0);
    gfx::bind(cells);
    gfx::draw_instanced(4, 4);
    gfx::end_pass();
    let px = gfx::read_pixels(target);
    for y in 3 {
        var line: u8[>..] = [];
        for x in 4 {
            let i = (y * 4 + x) * 4;
            format(line, if x > 0 { "  " } else { "" }, px[i], ",", px[i + 1], ",", px[i + 2], ",", px[i + 3]);
        }
        print("row ", y, ": ", line);
    }
    gfx::close();
}
