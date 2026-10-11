layout(binding = 0) uniform sampler2D uAtlas;

in vec2 vUV;

out vec4 fragColor;

// Vanilla "crumbling": multiplied into the scene (blend DST_COLOR, SRC_COLOR), so
// mid-gray leaves the block unchanged and darker texels crack it.
void main() {
    vec4 c = texture(uAtlas, vUV);
    if (c.a < 0.1) discard;
    fragColor = c;
}
