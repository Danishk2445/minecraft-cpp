#include "fog.glsl"

layout(binding = 0) uniform sampler2D uTexture;
layout(binding = 1) uniform sampler2D uLightmap;
uniform float uAlphaCutoff; // < 0: no alpha test
uniform vec4 uFogColor;
uniform vec4 uFog;          // envStart, envEnd, rdStart, rdEnd

in vec2 vUV;
in vec4 vColor;
in vec2 vLightUV;
in float vSpherical;
in float vCylindrical;

out vec4 fragColor;

void main() {
    vec4 color = texture(uTexture, vUV) * vColor;
    if (color.a < uAlphaCutoff) discard;
    color.rgb *= texture(uLightmap, vLightUV).rgb;
    fragColor = applyFog(color, vSpherical, vCylindrical, uFog, uFogColor);
}
