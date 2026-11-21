layout(set = 1, binding = 0) uniform Rect { vec4 r; vec4 z; };
void main() {
    int i = gl_VertexIndex;
    float u = (i == 1 || i == 2 || i == 4) ? r.z : r.x;
    float v = (i == 2 || i == 4 || i == 5) ? r.w : r.y;
    gl_Position = vec4(u / z.y * 2.0 - 1.0, 1.0 - v / z.z * 2.0, z.x, 1.0);
}
