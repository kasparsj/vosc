// references:
// http://madebyevan.com/shaders/grid/
// Hash without sine: https://www.shadertoy.com/view/4djSRW

const mat3 rotationMatrix = mat3(1.0,0.0,0.0,0.0,0.7,-0.7,0.0,0.7,0.7);

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

vec3 shading( vec3 ro, vec3 rd )
{
    vec3 c = vec3(rd.y*2.0) * 0.1;
    vec3 sk = ro;
    if (raymarch(ro,rd))
    {
        vec2 p = ro.xz;
        vec2 g = abs(fract(p - 0.5) - 0.5) / fwidth(p);
        float s = min(g.x, g.y);
        float f = min(length(ro-sk)/64.,1.);
        return mix(1.5-vec3(s,s,s), c, f);
    }
    return vec3(0.0);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = (2.*fragCoord.xy - iResolution.xy)/iResolution.y;
    vec3 ro = vec3(0.5,25,iTime * 5.0);
    vec3 rd = normalize(vec3(uv,2.0)) * rotationMatrix;
    fragColor = vec4(shading(ro,rd), 1.0);
}
