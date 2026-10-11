layout(binding = 0) uniform sampler2D uAtlas;
uniform float uAlphaCutoff; // < 0: no alpha test

in vec2 vUV;
in vec3 vColor;
in vec2 vLight;

out vec4 fragColor;

void main() {
    vec4 tex = texture(uAtlas, vUV);
    if (tex.a < uAlphaCutoff) discard;
    // Placeholder light curve until the lightmap arrives (M3).
    float light = max(vLight.x, vLight.y);
    float brightness = mix(0.08, 1.0, light * light);
    fragColor = vec4(tex.rgb * vColor * brightness, tex.a);
}
