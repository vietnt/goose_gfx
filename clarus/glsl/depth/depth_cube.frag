layout(set = 2, binding = 0) uniform samplerCube depth;
layout(location = 0) flat in ivec4 v_at;
layout(location = 0) out float result;
void main() {
    float n = float(textureSize(depth, v_at.w).x);
    vec2 st = (vec2(v_at.xy + ivec2(gl_FragCoord.xy)) + 0.5) / n * 2.0 - 1.0;
    float s = st.x, t = st.y;
    vec3 d = v_at.z == 0 ? vec3(1.0, -t, -s)
           : v_at.z == 1 ? vec3(-1.0, -t, s)
           : v_at.z == 2 ? vec3(s, 1.0, t)
           : v_at.z == 3 ? vec3(s, -1.0, -t)
           : v_at.z == 4 ? vec3(s, -t, 1.0)
           : vec3(-s, -t, -1.0);
    result = textureLod(depth, d, float(v_at.w)).r;
}
