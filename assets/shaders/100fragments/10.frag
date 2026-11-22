#version 150

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;

out vec4 fragColor;

void main(void) {
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 st = fragCoord.xy / resolution.xy;
    vec2 uv = -1. + 2. * st;
    float div = 1.0;
    float speed = 0.2;
    float r = mod(st.x + mod(time / div * speed, 1.0), 1.0 / div) * div;
    float g = mod(st.y + mod(time / div * speed * 1.0, 1.0), 1.0 / div) * div;
    float b = mod(st.x - mod(time / div * speed, 1.0), 1.0 / div) * div;
    fragColor = vec4(r, g, b, 1.0);
}
