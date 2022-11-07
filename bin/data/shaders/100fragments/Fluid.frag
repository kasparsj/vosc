#version 150

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;

out vec4 fragColor;

void main() {
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 p = fragCoord.xy / resolution.xy * 3.0;
    for(float i = 1.0; i < 10.0; i++){
        p.x += 0.3 / i * sin(i * 3.0 * p.y + time * 0.1 + cos((time / (100. * i)) * i));
        p.y += 0.4 / i * cos(i * 3.0 * p.x + time * 2.1 + sin((time / (200. * i)) * i));
    }
    float r = cos(p.x + p.y + 2.)*.5+.5;
    float g = sin(p.x + p.y + 1.)*.5+.5;
    float b = (sin(p.x + p.y + 1.) + cos(p.x + p.y + 1.))*.25+.5;
    fragColor = vec4(r, g, b, 1);
}
