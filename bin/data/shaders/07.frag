#version 120

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;

void main (void){
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 uv = vec2(0.5, 0.5 * (resolution.y / resolution.x)) - fragCoord.xy / resolution.x;
    float dist = sqrt(dot(uv, uv));
    float radius = cos(time * 10.0) * 0.1 + 0.2;
    float border = sin(time * 10.0) * 0.2 + 0.05;
    float t = smoothstep(radius + border, radius - border, dist);
    gl_FragColor = vec4(t * 0.6, t, t * 1.5, 1.0);
}
