#version 150

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;

out vec4 fragColor;

void main() {
    vec2 m = vec2(0.5, 0.5 * (resolution.y / resolution.x));
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 p = fragCoord.xy / resolution.x;
    float speed =  2.0;
    float r = pow(sin(length(m - p) * 10.0 - time), 3.0) * 2.0;
    float g = pow(sin(length(m - p) * 11.0 - time), 3.0) * 2.0;
    float b = pow(sin(length(m - p) * 12.0 - time), 3.0) * 2.0;
    fragColor = vec4(r, g, b, 1.0);
}
