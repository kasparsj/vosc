#version 330 core

uniform sampler2DArray tex;
uniform int texIndex;

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

out vec4 fragColor;

void main() {
    fragColor = texture(tex, vec3(vertex.texcoord, texIndex));
}
