// Block gallery and model sheets: flat-shaded, pre-tinted block quads.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;

out vec2 vUV;
out vec4 vColor;

void main() {
    gl_Position = uViewProj * vec4(aPos - uCameraPos, 1.0);
    vUV = aUV;
    vColor = aColor;
}
