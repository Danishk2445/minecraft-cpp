// A full-screen triangle for the menu blur (vanilla post/blur.vsh).
out vec2 vUV;

void main() {
    const vec2 corners[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(corners[gl_VertexID], 0.0, 1.0);
    vUV = corners[gl_VertexID] * 0.5 + 0.5;
}
