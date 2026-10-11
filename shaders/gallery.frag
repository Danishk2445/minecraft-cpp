layout(binding = 0) uniform sampler2D uAtlas;
uniform float uAlphaCutoff;

in vec2 vUV;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec4 color = texture(uAtlas, vUV) * vColor;
    if (color.a < uAlphaCutoff) discard;
    fragColor = color;
}
