// Sky geometry (dome disc, sunrise fan, sun, moon, stars, clouds): camera-relative
// positions with per-vertex color and optional texture coordinates.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

uniform mat4 uProjView; // projection * view (rotation only)
uniform mat4 uModel;

out vec4 vColor;
out vec2 vUV;
out float vSpherical;
out float vCylindrical;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    gl_Position = uProjView * world;
    vColor = aColor;
    vUV = aUV;
    vSpherical = length(world.xyz);
    vCylindrical = max(length(world.xz), abs(world.y));
}
