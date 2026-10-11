// Screen-space wide lines (block selection outline): each segment is a quad
// whose corners are pushed sideways in clip space, so the width stays in pixels.
layout(location = 0) in vec3 aPos;   // camera-relative endpoint
layout(location = 1) in vec3 aOther; // the segment's other endpoint
layout(location = 2) in float aSide; // -1 or +1

uniform mat4 uViewProj;
uniform vec2 uViewport; // pixels
uniform float uWidth;   // pixels

void main() {
    vec4 a = uViewProj * vec4(aPos, 1.0);
    vec4 b = uViewProj * vec4(aOther, 1.0);
    vec2 dir = (b.xy / b.w - a.xy / a.w) * uViewport;
    dir = length(dir) > 1e-6 ? normalize(dir) : vec2(1.0, 0.0);
    vec2 offset = vec2(-dir.y, dir.x) * uWidth / uViewport;
    gl_Position = a + vec4(offset * aSide * a.w, 0.0, 0.0);
}
