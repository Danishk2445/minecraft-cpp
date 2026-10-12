// The title panorama: cube faces around the camera (positions are directions).
layout(location = 0) in vec3 aDir;
layout(location = 1) in vec2 aUV;

uniform mat4 uViewProj;

out vec2 vUV;

void main() {
    gl_Position = uViewProj * vec4(aDir, 1.0);
    vUV = aUV;
}
