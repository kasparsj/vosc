// 362 chars - Thanks to timestamp and Fabrice, this is 7 chars lighter!
#define m  *= mat2( cos( vec4(0,33,55,0) + t*//
#define M  ( s.xz m.4)), s.xy m.3)) ,                        \
               length(s + sin(t*.7))                         \
             * log( length(s) + 1. )                         \
             + sin( sin(sin(s+=s+t).y+s).z + s ).x * .5 - 1. \
           )

void mainImage(out vec4 o, vec2 u){
    vec3 p,s,O,R = iResolution;
    for( float t = iTime, d = 2.5, r;
         R.z++ < 7.;
         o.xyz = O = max(O+.7-r*.28,O)
                   * ( vec3(.1,.3,.4) - vec3(10,5,6)*(M-r)/4. )
       )
        s = p = vec3( (u - .5*R.xy ) / R.y * d, 5.-d ),
        d += min( r = M, 1. ),
        s = p + .1;
}


// 369 chars - coyote uses some Sith magic to battle Jedi Fabrice!! 
/**
#define m *= mat2( cos( vec4(0,33,55,0) + t*

#define M \
  ( s.xz m.4)), s.xy m.3)),        \
    length(s+sin(t*.7))*log(length(s)+1.) + sin(q+sin(q+sin( q= s+s+t ).y).z).x*.5 - 1. )

void mainImage(out vec4 o, vec2 u) {
    o*=0.;
    vec3 q,p,s, R=iResolution;
    for( float t=iTime, d=2.5, r; R.z++<7.; 
          o *= vec4(.1,.3,.4,0) - vec4(10, 5, 6,0) * (M-r) /4. )
        s = p = vec3( ( u - .5*R.xy ) /R.y * d , 5.-d ),
        d += min( r = M, 1.),
        s = p + .1,
        o += max( .7-r*.28 ,0.);
}        
/*


// 354 chars - Just when I think it can't get any smaller, Fabrice pulls a MIRACLE!!

#define m *= mat2( cos( vec4(0,33,55,0) + t*

#define M                          \
  ( s=p,                           \
    s.xz m.4)), s.xy m.3)),        \
    q = s+s+t,                     \
    length(s+sin(t*.7))*log(length(s)+1.) + sin(q.x+sin(q.z+sin(q.y)))*.5 - 1. )


#define mainImage(o,u)	           \
   vec3 q,p,s, R=iResolution;      \
    for( float t=iTime, d=2.5, r; R.z++<7.; d += min(r,1.)) \
		p = vec3( ( u - .5*R.xy ) /R.y * d , 5.-d ),        \
        r = M,                     \
        o += max( .7-r*.28 ,0.),   \
        p += .1,                   \
        o *= vec4(.1,.3,.4,0) - vec4(10, 5, 6,0) * (M-r) /4.


        
// 387 chars - Once again, Fabrice reduces what CAN'T be reduced. ;-)

#define m(a) mat2(cos(t*a+vec4(0,33,55,0)))

#define M(p)                       \
  ( s=p, t=iTime,                  \
    s.xz*= m(.4), s.xy*= m(.3),    \
    q = s+s+t,                     \
    length(s+sin(t*.7))*log(length(s)+1.) + sin(q.x+sin(q.z+sin(q.y)))*.5 - 1. )


#define mainImage(o,u)	           \
   vec3 q,p,s, R=iResolution;        \
    for( float t, d=2.5, r, i=0.; i++<6.; d += min(r,1.)) \
		p.xy = ( u - .5*R.xy ) /R.y * d,                  \
        p.z = 5.-d,                                       \
        o += smoothstep(2.5, 0., r = M(p) ) *.7,          \
        o *= vec4(.1,.3,.4,0) + vec4(5, 2.5, 3,0)*max(( r-M(p+.1) )*.5, -.1) 


        
// 401 chars - Golfing legend Fabrice did it again!!

#define m(a) mat2(cos(a+vec4(0,33,55,0)))
vec3 q,p;
float t, d=2.5, r;

float M(vec3 p){
    t=iTime;
    p.xz*= m(t*.4);p.xy*= m(t*.3);
    q = p+p+t;
    return length(p+sin(t*.7))*log(length(p)+1.) + sin(q.x+sin(q.z+sin(q.y)))*.5 - 1.;
}

#define mainImage(o,u)		\
for(int i=0; i++<6;)	\
		p.xy = (u/iResolution.y - vec2(.9,.5))*d,	\
        p.z = 5.-d,	\
        o += smoothstep(2.5, 0., r = M(p))*.7,	\
        o *= vec4(.1,.3,.4,0) + vec4(5, 2.5, 3,0)*clamp((r - M(p+.1))*.5, -.1, 1.),	\
        d += min(r, 1.)


        
// Thanks to BigWings I made a few chars disappear:

#define m(a) mat2(cos(a+.8*vec4(6,0,0,2)))
vec3 q,p;
float t, d=2.5, r, i=0.;

float M(vec3 p){
    t=iTime;
    p.xz*= m(t*.4);p.xy*= m(t*.3);
    q = p+p+t;
    return length(p+sin(t*.7))*log(length(p)+1.) + sin(q.x+sin(q.z+sin(q.y)))*.5 - 1.;
}

#define mainImage(o,u)		\
while(i++<6.){	\
		p = vec3(u/iResolution.y - vec2(.9,.5), -1)*d;	\
        p.z += 5.;	\
        o += smoothstep(2.5, 0., r = M(p))*.7;	\
        o *= vec4(.1,.3,.4,0) + vec4(5, 2.5, 3,0)*clamp((r - M(p+.1))*.5, -.1, 1.);	\
        d += min(r, 1.);}



// 600 chars - Original shader by nimitz:

#define t iTime
mat2 m(float a){float c=cos(a), s=sin(a);return mat2(c,-s,s,c);}
float map(vec3 p){
    p.xz*= m(t*0.4);p.xy*= m(t*0.3);
    vec3 q = p*2.+t;
    return length(p+vec3(sin(t*0.7)))*log(length(p)+1.) + sin(q.x+sin(q.z+sin(q.y)))*0.5 - 1.;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ){	
	vec2 p = fragCoord.xy/iResolution.y - vec2(.9,.5);
    vec3 cl = vec3(0.);
    float d = 2.5;
    for(int i=0; i<=5; i++)	{
		vec3 p = vec3(0,0,5.) + normalize(vec3(p, -1.))*d;
        float rz = map(p);
		float f =  clamp((rz - map(p+.1))*0.5, -.1, 1. );
        vec3 l = vec3(0.1,0.3,.4) + vec3(5., 2.5, 3.)*f;
        cl = cl*l + (1.-smoothstep(0., 2.5, rz))*.7*l;
		d += min(rz, 1.);
	}
    fragColor = vec4(cl, 1.);
}
*/