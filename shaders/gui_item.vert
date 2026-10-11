// GUI item icons: GUI pixels (y down) with depth toward the viewer.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // tint * GUI item lighting

uniform mat4 uProj;

out vec2 vUV;
out vec4 vColor;

void main() {
    gl_Position = uProj * vec4(aPos, 1.0);
    vUV = aUV;
    vColor = aColor;
}
