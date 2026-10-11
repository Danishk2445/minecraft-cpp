layout(binding = 0) uniform sampler2D uAtlas;
layout(binding = 1) uniform sampler2D uLightmap;

in vec2 vUV;
in vec4 vColor;
in vec2 vLightUV;

out vec4 fragColor;

void main() {
    vec4 c = texture(uAtlas, vUV) * vColor;
    if (c.a < 0.1) discard;
    fragColor = vec4(c.rgb * texture(uLightmap, vLightUV).rgb, c.a);
}
