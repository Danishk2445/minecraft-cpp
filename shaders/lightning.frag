#include "fog.glsl"

uniform vec4 uFog; // envStart, envEnd, rdStart, rdEnd

in vec4 vColor;
in float vSpherical;
in float vCylindrical;

out vec4 fragColor;

void main() {
    // Vanilla rendertype_lightning.fsh: faded by the fog rather than tinted.
    float f = max(linearFog(vSpherical, uFog.x, uFog.y), linearFog(vCylindrical, uFog.z, uFog.w));
    fragColor = vColor * (1.0 - f);
}
