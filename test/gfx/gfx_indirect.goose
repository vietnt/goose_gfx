// Indirect draws and dispatches: parameters uploaded, and written by a
// compute shader for the GPU to read back as parameters. Four pixel columns
// each have a quad of their own in one vertex buffer, holding the column's
// value; draws add up into the target, so a column reads its value times
// the instances drawn over it, 0 where no command reached it. Which
// commands run (by offset and count), first_vertex, instances, an index
// buffer's vertex_offset and a dispatch's group count all show in what is
// read back.
import gfx from "../..";
import std;
import os;

struct V { x: f32, y: f32, value: f32 }

fn main() {
    guard gfx::open_headless(4, 1) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    // Column c: two triangles over it, vertices 6c to 6c + 5, value c + 1.
    var verts: V[>..] = [];
    for c in 4 {
        let x0 = c as f32 / 2.0 - 1.0;
        let x1 = (c + 1) as f32 / 2.0 - 1.0;
        let v = (c + 1) as f32;
        verts.append([V { x: x0, y: -1.0, value: v }, V { x: x1, y: -1.0, value: v },
                      V { x: x0, y: 1.0, value: v }, V { x: x1, y: -1.0, value: v },
                      V { x: x1, y: 1.0, value: v }, V { x: x0, y: 1.0, value: v }]);
    }
    let vb = gfx::vertex_buffer(bytes_of(verts));
    let indices: u16[6] = [0, 1, 2, 3, 4, 5];
    let ib = gfx::index_buffer(bytes_of(indices));
    let add = gfx::pipeline(embed_shader("vert", """
        layout(location = 0) in vec2 pos;
        layout(location = 1) in float value;
        layout(location = 0) flat out float v_value;
        void main() { v_value = value; gl_Position = vec4(pos, 0.0, 1.0); }
        """), embed_shader("frag", """
        layout(location = 0) flat in float v_value;
        layout(location = 0) out vec4 c;
        void main() { c = vec4(v_value / 255.0, 0.0, 0.0, 1.0); }
        """), gfx::PipelineDesc { blend: gfx::BLEND_CUSTOM, blend_dst: gfx::FACTOR_ONE });

    // Three commands; the draw runs the last two, from byte 16.
    let uploaded: gfx::DrawCommand[3] = [
        gfx::DrawCommand { vertices: 6, instances: 2, first_vertex: 6, first_instance: 0 },
        gfx::DrawCommand { vertices: 6, instances: 3, first_vertex: 18, first_instance: 0 },
        gfx::DrawCommand { vertices: 6, instances: 1, first_vertex: 0, first_instance: 0 }];
    let args = gfx::buffer(gfx::BUFFER_INDIRECT, bytes_of(uploaded));
    let indexed: gfx::IndexedDrawCommand[1] = [
        gfx::IndexedDrawCommand { indices: 6, instances: 2, first_index: 0, vertex_offset: 6,
                                  first_instance: 0 }];
    let indexed_args = gfx::buffer(gfx::BUFFER_INDIRECT, bytes_of(indexed));

    // A compute shader writes a draw (column 2, five instances) and a
    // dispatch of three groups.
    let gpu_draw = gfx::buffer(gfx::BUFFER_INDIRECT | gfx::BUFFER_COMPUTE_WRITE, 16, "");
    let gpu_dispatch = gfx::buffer(gfx::BUFFER_INDIRECT | gfx::BUFFER_COMPUTE_WRITE, 12, "");
    let write_args = gfx::compute_pipeline(embed_shader("comp", """
        layout(local_size_x = 1) in;
        layout(std430, set = 1, binding = 0) buffer Draw { uint d[]; };
        layout(std430, set = 1, binding = 1) buffer Dispatch { uint g[]; };
        void main() {
            d[0] = 6u; d[1] = 5u; d[2] = 12u; d[3] = 0u;
            g[0] = 3u; g[1] = 1u; g[2] = 1u;
        }
        """));
    let rw: gfx::Buffer[2] = [gpu_draw, gpu_dispatch];
    gfx::begin_compute(rw);
    gfx::bind(write_args);
    gfx::dispatch(1, 1, 1);
    gfx::end_compute();

    // Each group writes its own element: three of four.
    let groups = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 16, "");
    let mark = gfx::compute_pipeline(embed_shader("comp", """
        layout(local_size_x = 1) in;
        layout(std430, set = 1, binding = 0) buffer Out { uint o[]; };
        void main() { o[gl_WorkGroupID.x] = gl_WorkGroupID.x + 100u; }
        """));
    let out: gfx::Buffer[1] = [groups];
    gfx::begin_compute(out);
    gfx::bind(mark);
    gfx::dispatch_indirect(gpu_dispatch, 0);
    gfx::end_compute();

    let target = gfx::render_target(4, 1, gfx::RGBA8);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::bind(add);
    gfx::bind_vertex_buffer(vb);
    gfx::draw_indirect(args, 16, 2);
    gfx::draw_indirect(gpu_draw, 0, 1);
    gfx::bind_index_buffer(ib, 2);
    gfx::draw_indexed_indirect(indexed_args, 0, 1);
    gfx::end_pass();
    let px = gfx::read_pixels(target);
    var columns: u8[>..] = [];
    for x in 4 { columns.push(px[x * 4]); }
    print("columns: ", columns[0], " ", columns[1], " ", columns[2], " ", columns[3]);
    var marked: u32[>..] = [];
    gfx::read_buffer(groups, marked, 4);
    print("dispatched groups: ", marked);
    gfx::close();
}
