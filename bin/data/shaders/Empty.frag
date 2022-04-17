#version 120

#define DEFAULT_COLOR vec3(1, 1, 1)
#define MAX_VALUES 8

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;
uniform float values[MAX_VALUES];

void main() {
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    gl_FragColor = col;
}
