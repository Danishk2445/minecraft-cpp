// Vanilla post/box_blur.fsh: a box blur of radius r along uStep. Bilinear filtering
// reads two texels per sample, so it samples every other texel boundary, and the
// last texel (2r + 1 is odd) with half weight.
layout(binding = 0) uniform sampler2D uIn;
uniform vec2 uStep;   // one texel along the blur direction
uniform float uRadius;

in vec2 vUV;

out vec4 fragColor;

void main() {
    vec4 blurred = vec4(0.0);
    float actualRadius = round(uRadius);
    for (float a = -actualRadius + 0.5; a <= actualRadius; a += 2.0) {
        blurred += texture(uIn, vUV + uStep * a);
    }
    blurred += texture(uIn, vUV + uStep * actualRadius) / 2.0;
    fragColor = blurred / (actualRadius + 0.5);
}
