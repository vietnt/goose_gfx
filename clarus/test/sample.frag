layout(location = 0) out vec4 c;
layout(set = 2, binding = 0) uniform sampler2D field;
void main() { c = texture(field, gl_FragCoord.xy / vec2(4.0, 1.0)); }
