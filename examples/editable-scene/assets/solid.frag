#version 330
uniform vec4 globalColor;
out vec4 fragColor;
void main() {
    fragColor = globalColor;
}
