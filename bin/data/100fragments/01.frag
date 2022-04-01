#version 120

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;

void main() {
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 v_texcoord = fragCoord.xy / resolution.xy;
    vec2 p = v_texcoord * (sin(time * 100.0) * 200.0);
    float r = (sin(p.x * 0.3 + time * 90.0) * 1.0 + 0.5);
    float g = (sin(p.x * 0.2 + time * 100.0) * 1.0 + 0.5);
    float b = (sin(p.x * 0.1 + time * 104.0) * 1.0 + 0.5);
    gl_FragColor = vec4(r, g, b, 1.0);
}
