#version 150

uniform sampler2D tex;

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

out vec4 fragColor;

void main() {
    fragColor = texture(tex, vertex.texcoord);
}
