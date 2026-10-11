layout(binding = 0) uniform sampler2D uTexture;

in vec2 vUV;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec4 c = texture(uTexture, vUV) * vColor;
    if (c.a <= 0.0) discard;
    fragColor = c;
}
