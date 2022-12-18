#version 150

const vec3 DEFAULT_COLOR = vec3(1, 1, 1);

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int layer;
uniform vec4 color;
uniform int random;

out vec4 fragColor;

void main(void){
    vec2 m;
    float t;
    vec2 fragCoord = offset + gl_FragCoord.xy;
    for(float i = 0.0; i < 60.0; i+=1.0){
        m = vec2(sin(time * sin(i+3.0) *4.0) * 1.0, cos(time * cos(i + 3.0) * 4.0) * 1.0);
        vec2 pos = (fragCoord.xy * 1.0 - resolution) / min(resolution.x, resolution.y);
        t += sin(i * 2.0) / length(m - pos);
    }
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    fragColor = vec4(vec3(t) / 0.1 * col.rgb + vec3(0.0, 0.0, 0.5), col.a);
}
