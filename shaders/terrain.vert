// Chunk terrain: 16-byte vertices, section origin per draw (instanced attribute).
layout(location = 0) in uvec3 aPos;   // (p + 8) * 2048, section-local
layout(location = 1) in uvec2 aLight; // block, sky: level * 16
layout(location = 2) in vec2 aUV;     // atlas, unorm16
layout(location = 3) in uvec4 aColor; // tint * shade * AO, flags
layout(location = 4) in vec3 aOrigin; // section origin relative to the camera

uniform mat4 uViewProj;

out vec2 vUV;
out vec3 vColor;
out vec2 vLight; // block, sky in 0..1

void main() {
    vec3 pos = aOrigin + vec3(aPos) * (1.0 / 2048.0) - 8.0;
    gl_Position = uViewProj * vec4(pos, 1.0);
    vUV = aUV;
    vColor = vec3(aColor.rgb) / 255.0;
    vLight = vec2(aLight) / 240.0;
}
