// A dusk sky over a field, with sun rings: what the demo's ui draws over.
layout(set = 3, binding = 0) uniform Scene { vec4 sun; };
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
void main() {
 vec3 top = vec3(0.10, 0.12, 0.32), horizon = vec3(0.95, 0.55, 0.30), ground = vec3(0.16, 0.30, 0.14);
 vec3 c = v_uv.y < 0.62 ? mix(top, horizon, v_uv.y / 0.62) : mix(ground * 1.4, ground, (v_uv.y - 0.62) / 0.38);
 vec2 d = (v_uv - sun.xy) * vec2(sun.z, 1.0);
 float r = length(d);
 c = mix(c, vec3(1.0, 0.92, 0.6), smoothstep(sun.w, sun.w * 0.8, r));
 c += 0.08 * (0.5 + 0.5 * cos(r * 60.0)) * smoothstep(0.5, 0.0, r) * step(v_uv.y, 0.62);
 result = vec4(c, 1.0);
}
