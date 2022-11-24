#version 150

const vec3 DEFAULT_COLOR = vec3(0.0, 0.35, 0.5);

uniform float time;
uniform vec2 resolution;

out vec4 fragColor;

#define so fract(sin(time)*123.456)

float det=.001, br=0., tub=0., hit=0.;
vec3 pos, sphpos;
mat3 lookat(vec3 dir, vec3 up) {
  vec3 rt=normalize(cross(dir,up));
  return mat3(rt,cross(rt,dir),dir);
}
vec3 path(float t) {
  return vec3(sin(t+cos(t)*.5)*.5,cos(t*.5),t);
}
mat2 rot(float a) {
  float s=sin(a);
  float c=cos(a);
  return mat2(c,s,-s,c);
}
vec3 fractal(vec2 p) {
  p=fract(p*.1);
  float m=1000.;
  for (int i=0; i<7; i++) {
    p=abs(p)/clamp(abs(p.x*p.y),.25,2.)-1.2;
    m=min(m,abs(p.y)+fract(p.x*.3+time*.5+float(i)*.25));
  }
  m=exp(-6.*m);
  return m*vec3(abs(p.x),m,abs(p.y));
}

float coso(vec3 pp) {
  pp*=.7;
  pp.xy*=rot(pp.z*2.);
  pp.xz*=rot(time*2.);
  pp.yz*=rot(time);
  float sph=length(pp)-.04;
  sph-=length(sin(pp*40.))*.05;
  sph=max(sph,-length(pp)+.11);
  float br2=length(pp)-.03;
  br2=min(br2,length(pp.xy)+.005);
  br2=min(br2,length(pp.xz)+.005);
  br2=min(br2,length(pp.yz)+.005);
  br2=max(br2,length(pp)-1.);
  br=min(br2,br);
  float d=min(br,sph);
  return d;
}


float de(vec3 p) {
  hit=0.;
  br=1000.;
  vec3 pp=p-sphpos;
  p.xy-=path(p.z).xy;
  p.xy*=rot(p.z+time*.5);
  float s=sin(p.z*.5+time*.5);
  p.xy*=1.3-s*s*.7;
  
  for(int i=0; i<6; i++) {
    p=abs(p)-.4;
  }
  pos=p;
  tub=-length(p.xy)+.45+sin(p.z*10.)*.1*smoothstep(.4,.5,abs(.5-fract(p.z*.05))*2.);
  float co=coso(pp);
  co=min(co,coso(pp+.7));
  co=min(co,coso(pp-.7));
  float d=min(tub,co);
  if (d==tub) hit=step(fract(.1*length(sin(p*10.))),.05);
  return d*.3;
}

vec3 march(vec3 from, vec3 dir) {
  vec2 uv=vec2(atan(dir.x,dir.y)+time*.5,length(dir.xy)+sin(time*.2));
  vec3 col=fractal(uv);
  float d=0.,td=0.,g=0., ref=0., ltd=0., li=0.;
  vec3 p=from;
  for (int i=0; i<200; i++) {
    p+=dir*d;
    d=de(p);
    if (d<det && ref==0. && hit==1.) {
      vec2 e=vec2(0.,.1);
      vec3 n=normalize(vec3(de(p+e.yxx),de(p+e.xyx),de(p+e.xxy))-de(p));
      p-=dir*d*2.;
      dir=reflect(dir,n);
      ref=1.;
      td=0.;
      ltd=td;
      continue;
    }
    if (d<det || td>5.) break;
    td+=d;
    g+=.1/(.1+br*13.);
    li+=.1/(.1+tub*5.);
  }
  g=max(g,li*.15);
  float f=1.-td/3.;
  if (ref==1.) f=1.-ltd/3.;
  if (d<.01) {
    col=vec3(1.);
    vec2 e=vec2(0.,det);
    vec3 n=normalize(vec3(de(p+e.yxx),de(p+e.xyx),de(p+e.xxy))-de(p));
    col=vec3(n.x)*.7;
    col+=fract(pos.z*5.)*vec3(.2,.1,.5);
    col+=fractal(pos.xz*2.);
    if (tub>.01) col=vec3(0.);
  }
  col*=f;
  vec3 glo=g*.1*vec3(2.,1.,2.)*(.5+so*1.5)*.5;
  glo.rb*=rot(dir.y*1.5);
  col+=glo;
  col*=vec3(.8,.7,.7);
  col=mix(col,vec3(1.),ref*.3);
  return col;
}

void main( )
{
  vec2 uv = vec2(gl_FragCoord.x / resolution.x, gl_FragCoord.y / resolution.y);
  uv -= 0.5;
  uv /= vec2(resolution.y / resolution.x, 1);
  float t=time;
  vec3 from= path(t);
  if (mod(time,10.)>5.) from=path(floor(t/4.+.5)*4.);
  sphpos=path(t+.5);
  from.x+=.2;
  vec3 fw=normalize(path(t+.5)-from);
  vec3 dir=normalize(vec3(uv,.5));
  dir=lookat(fw,vec3(fw.x*2.,1.,0.))*dir;
  dir.xz+=sin(time)*.3;
  vec3 col=march(from,dir);
  col=mix(vec3(.5)*length(col),col,.8);
  fragColor =vec4(col,0.5);
}

