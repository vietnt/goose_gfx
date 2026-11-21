layout(set = 2, binding = 0) uniform sampler2D depth;
layout(location = 0) flat in ivec4 v_at;
layout(location = 0) out float result;
void main() { result = texelFetch(depth, v_at.xy + ivec2(gl_FragCoord.xy), v_at.w).r; }
