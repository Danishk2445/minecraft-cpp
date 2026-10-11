// Port of the pack's shaders/include/fog.glsl: the larger of environmental fog
// (spherical distance) and render-distance fog (cylindrical distance).
float linearFog(float d, float start, float end) {
    if (d <= start) return 0.0;
    if (d >= end) return 1.0;
    return (d - start) / (end - start);
}

// fog = (envStart, envEnd, renderDistanceStart, renderDistanceEnd)
vec4 applyFog(vec4 color, float spherical, float cylindrical, vec4 fog, vec4 fogColor) {
    float f = max(linearFog(spherical, fog.x, fog.y), linearFog(cylindrical, fog.z, fog.w));
    return vec4(mix(color.rgb, fogColor.rgb, f * fogColor.a), color.a);
}
