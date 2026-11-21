layout(set = 3, binding = 0) uniform Color { vec4 c; };
layout(location = 0) out vec4 result;
void main() { result = c; }
