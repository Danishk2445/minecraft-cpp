#include "fog.glsl"

layout(binding = 0) uniform sampler2D uTexture;
uniform int uTextured;   // 0: color only, 1: color * texture
uniform vec4 uColor;     // modulator
uniform vec4 uFogColor;
uniform vec4 uFog;       // envStart, envEnd, rdStart, rdEnd

in vec4 vColor;
in vec2 vUV;
in float vSpherical;
in float vCylindrical;

out vec4 fragColor;

void main() {
    vec4 c = vColor * uColor;
    if (uTextured != 0) c *= texture(uTexture, vUV);
    if (c.a <= 0.0) discard;
    fragColor = applyFog(c, vSpherical, vCylindrical, uFog, uFogColor);
}
