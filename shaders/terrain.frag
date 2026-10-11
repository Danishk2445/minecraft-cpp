#include "fog.glsl"

layout(binding = 0) uniform sampler2D uAtlas;
layout(binding = 1) uniform sampler2D uLightmap;
uniform float uAlphaCutoff; // < 0: no alpha test
uniform vec4 uFogColor;
uniform vec4 uFog;          // envStart, envEnd, rdStart, rdEnd

in vec2 vUV;
in vec3 vColor;
in vec2 vLightUV;
in float vSpherical;
in float vCylindrical;

out vec4 fragColor;

void main() {
    vec4 tex = texture(uAtlas, vUV);
    if (tex.a < uAlphaCutoff) discard;
    vec3 light = texture(uLightmap, vLightUV).rgb;
    vec4 color = vec4(tex.rgb * vColor * light, tex.a);
    fragColor = applyFog(color, vSpherical, vCylindrical, uFog, uFogColor);
}
