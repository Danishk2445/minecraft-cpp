layout(binding = 0) uniform sampler2D uFace;

in vec2 vUV;

out vec4 fragColor;

void main() {
    fragColor = vec4(texture(uFace, vUV).rgb, 1.0);
}
