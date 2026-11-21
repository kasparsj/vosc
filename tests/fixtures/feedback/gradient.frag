#version 330
uniform vec2 resolution;
uniform vec4 globalColor;
out vec4 fragColor;
void main() { vec2 uv=gl_FragCoord.xy/resolution; fragColor=vec4(uv.x,.2,uv.y,1)*globalColor; }
