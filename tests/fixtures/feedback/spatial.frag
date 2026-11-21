#version 330
uniform sampler2D srctex;
uniform vec2 resolution;
out vec4 fragColor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy / resolution;
    float rs = ((p.x/7 + 2*(p.y/11))%5 == 0) ? 1.0 : 0.0;
    float gs = ((3*(p.x/5) + p.y/9)%7 < 2) ? 1.0 : 0.0;
    float r = texture(srctex, vec2(max(0.5,gl_FragCoord.x-3.0),gl_FragCoord.y)/resolution).r;
    float g = texture(srctex, vec2(gl_FragCoord.x,max(0.5,gl_FragCoord.y-2.0))/resolution).g;
    float b = .125 + .25*float(p.x%17)/16.0 + .125*float(p.y%13)/12.0;
    fragColor = vec4(.5*r+.25*rs,.5*g+.25*gs,b,1.0);
}
