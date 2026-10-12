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
in float vOverlay;

out vec4 fragColor;

void main() {
    vec4 color = texture(uTexture, vUV) * vColor;
    if (color.a < uAlphaCutoff) discard;
    // Vanilla entity.fsh: the overlay (the hurt red) before the light.
    color.rgb = mix(color.rgb, vec3(1.0, 0.0, 0.0), vOverlay);
    color.rgb *= texture(uLightmap, vLightUV).rgb;
    fragColor = applyFog(color, vSpherical, vCylindrical, uFog, uFogColor);
}
