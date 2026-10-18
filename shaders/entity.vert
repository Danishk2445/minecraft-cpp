// Entity models and shadows (vertices built on the CPU, camera-relative).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // tint * entity shading, alpha
layout(location = 3) in vec2 aLight; // block, sky levels 0..15
layout(location = 4) in vec4 aOverlay; // a color mixed in by its alpha (hurt red, TNT white)

uniform mat4 uViewProj;

out vec2 vUV;
out vec4 vColor;
out vec2 vLightUV;
out float vSpherical;
out float vCylindrical;
out vec4 vOverlay;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUV = aUV;
    vColor = aColor;
    vLightUV = (aLight + 0.5) / 16.0;
    vSpherical = length(aPos);
    vCylindrical = max(length(aPos.xz), abs(aPos.y));
    vOverlay = aOverlay;
}
