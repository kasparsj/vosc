#version 150

uniform sampler2DArray srctex;
uniform float time;
uniform vec2 resolution;
uniform int random;
uniform int texIndex;  // Layer index for array texture
uniform float nSides = 4.0;  // Number of sides/segments for kaleidoscope effect

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

out vec4 fragColor;

vec2 kaleidoscope(vec2 st, float nSides) {
    st -= 0.5;
    float r = length(st);
    float a = atan(st.y, st.x);
    float pi = 2.0 * 3.1416;
    a = mod(a, pi / nSides);
    a = abs(a - pi / nSides / 2.0);
    return r * vec2(cos(a), sin(a)) + 0.5;
}

void main() {
    vec2 uv = kaleidoscope(vertex.texcoord, nSides);
    // Sample from array texture using texIndex as the layer
    fragColor = texture(srctex, vec3(uv, texIndex));
}

