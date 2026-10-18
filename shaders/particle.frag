#include "fog.glsl"

layout(binding = 0) uniform sampler2D uAtlas;
layout(binding = 1) uniform sampler2D uLightmap;
uniform vec4 uFogColor;
uniform vec4 uFog; // envStart, envEnd, rdStart, rdEnd

in vec2 vUV;
in vec4 vColor;
in vec2 vLightUV;
in float vSpherical;
in float vCylindrical;

out vec4 fragColor;

void main() {
    // Vanilla particle.fsh: the lit texel, cut out below 0.1, then fog.
    vec4 c = texture(uAtlas, vUV) * vColor * texture(uLightmap, vLightUV);
    if (c.a < 0.1) discard;
    fragColor = applyFog(c, vSpherical, vCylindrical, uFog, uFogColor);
}
