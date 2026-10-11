// Block-breaking cracks drawn over the target block's shape.
layout(location = 0) in vec3 aPos; // camera-relative
layout(location = 1) in vec2 aUV;

uniform mat4 uViewProj;

out vec2 vUV;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUV = aUV;
}
