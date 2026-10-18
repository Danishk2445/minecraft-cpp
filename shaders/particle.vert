// Camera-facing particle quads (corners expanded on the CPU).
layout(location = 0) in vec3 aPos;   // camera-relative
layout(location = 1) in vec2 aUV;    // atlas
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aLight; // block, sky levels 0..15

uniform mat4 uViewProj;

out vec2 vUV;
out vec4 vColor;
out vec2 vLightUV;
out float vSpherical;
out float vCylindrical;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUV = aUV;
    vColor = aColor;
    vLightUV = (aLight + 0.5) / 16.0;
    vSpherical = length(aPos);
    vCylindrical = max(length(aPos.xz), abs(aPos.y));
}
