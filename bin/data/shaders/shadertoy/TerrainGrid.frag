#version 150

const vec3 DEFAULT_COLOR = vec3(1, 1, 1);

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;

out vec4 fragColor;

// references:
// http://madebyevan.com/shaders/grid/
// Hash without sine: https://www.shadertoy.com/view/4djSRW

const mat3 rotationMatrix = mat3(1.0,0.0,0.0,0.0,0.7,-0.5,0.0,0.7,0.7);

float hash(vec2 p)
{
    vec3 p3  = fract(vec3(p.xyx) * 10.21);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

float noise (vec2 P)
{
    float size = 256.0;
    float s = (1.0 / size);
    vec2 pixel = P * size + 0.5;
    vec2 f = fract(pixel);
    pixel = (floor(pixel) / size) - vec2(s/2.0, s/2.0);
    float C11 = hash(pixel + vec2( 0.0, 0.0));
    float C21 = hash(pixel + vec2( s, 0.0));
    float C12 = hash(pixel + vec2( 0.0, s));
    float C22 = hash(pixel + vec2( s, s));
    float x1 = mix(C11, C21, f.x);
    float x2 = mix(C12, C22, f.x);
    return mix(x1, x2, f.y);
}

float fbm( vec2 p )
{
    float a = 0.5, b = 0.0, t = 0.0;
    for (int i=0; i<5; i++)
    {
        b *= a; t *= a;
        b += noise(p);
        t += 1.0; p /= 2.0;
    }
    return b /= t;
}

float map( vec3 p )
{
    float h = p.y - 20.0 * fbm(p.xz*0.003);
    return max( min( h, 0.55), p.y-20.0 );
}

bool raymarch( inout vec3 ro, vec3 rd)
{
    float t = 0.0;
    for (int i=0; i<128; i++)
    {
        float d = map(ro+rd*t);
        t+=d;
        if (d<t*0.001)
        {
            ro+=t*rd;
            return true;
        }
    }
    return false;
}

float shading( vec3 ro, vec3 rd )
{
    float c = rd.y * 2.0 * 0.1;
    vec3 sk = ro;
    if (raymarch(ro,rd))
    {
        vec2 p = ro.xz;
        vec2 g = abs(fract(p - 0.5) - 0.5) / fwidth(p);
        float s = min(g.x, g.y);
        float f = min(length(ro-sk)/64.,1.);
        return mix(1.5-s, c, f);
    }
    return 0;
}

void main(void)
{
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 uv = (2.*fragCoord.xy - resolution.xy)/resolution.y;
    vec3 ro = vec3(0.5,25,time * 5.0);
    vec3 rd = normalize(vec3(-uv,2.0)) * rotationMatrix;
    float bri = shading(ro,rd);
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    fragColor = vec4(col.rgb, col.a * bri);
}
