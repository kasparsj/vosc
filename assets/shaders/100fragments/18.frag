#version 150

uniform float time;
uniform vec2 resolution;

out vec4 fragColor;

void main(void){
    vec2 st = gl_FragCoord.xy / resolution.xy;
    float r = abs(sin(st.x + time));
    float g = abs(cos(st.y + time));
    float b = 0.5;
    fragColor = vec4(r, g, b, 1.0);
}
