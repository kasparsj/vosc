#version 150

const vec3 DEFAULT_COLOR = vec3(0.15, 0.75, 1.0);

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;
uniform float glow;

out vec4 fragColor;

void main(void){
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 st = fragCoord.xy / resolution.xy;
    st -= 0.5;
    float bri = 0;
    for(float i = 0.0; i < 12.0; i++){
        st.y += sin(st.x * 4.0 + time * 2.0 + i * 40.0) * 0.2 * cos(time + (i + 2.0) * 300.0);
        bri += (1.0 - pow(abs(st.y), glow * 0.03));
    }
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    fragColor = vec4(col.rgb * bri, col.a);
}
