// Lightning bolts: position and color, camera-relative (vanilla rendertype_lightning).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uViewProj;

out vec4 vColor;
out float vSpherical;
out float vCylindrical;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vColor = aColor;
    vSpherical = length(aPos);
    vCylindrical = max(length(aPos.xz), abs(aPos.y));
}
