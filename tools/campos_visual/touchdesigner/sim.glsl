// sim.glsl — GLSL TOP (pixel shader). Estado de cada partícula en un pixel RGBA32F: xy = posición (unidades de mundo), zw = velocidad.
// Es el MISMO modelo que el modo "Humo GPU" del navegador (tools/campos_visual/index.html), portado a TouchDesigner:
//   clase 2 HAZ    : gravedad newtoniana alrededor del núcleo; se relanza en grupos con cada golpe (arcos elípticos paralelos)
//   clase 1 NÚCLEO : resorte corto + remolino + ruido + viento
//   clase 0 VENA   : sigue un campo sin divergencia (curl) con 3 octavas; nace en el núcleo y muere al azar
// Uniformes (vec0..vec5 del GLSL TOP, los escribe frame_exec.py cada cuadro):
uniform vec4 uA;   // x=dt  y=T(s)  z=caos  w=U (unidades de mundo por "unidad de 800 px")
uniform vec4 uB;   // x=graves  y=medios  z=agudos  w=R0 (radio del núcleo)
uniform vec4 uC;   // x=burstOn y=burstGrupo z=burstAngulo w=burstVelocidad
uniform vec4 uD;   // x=impulso radial  y=impulso tangencial  z=chispeo  w=número de cuadro
uniform vec4 uK0;  // núcleo principal: xy=posición zw=velocidad (los 3 satélites se calculan aquí con las mismas fórmulas que el navegador)
uniform vec4 uW;   // x,y=viento  z=expansión (exp)  w=remolino (swl)

layout(location = 0) out vec4 fragColor;

uint hu(uint x){ x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return x; }
float hf(uint x){ return float(hu(x))/4294967296.0; }
int cls(uint id){ float h=hf(id*23u+7u); return h<0.004?2:(h<0.12?1:0); }   // 0 vena · 1 núcleo · 2 haz

vec3 orb(int i,float t){ float k=float(i); float a=t*(0.23+0.11*k)*0.95+k*2.1, rr=uB.w*2.4*(1.0+0.6*sin(t*0.17*k+k))*(1.0+0.6*uW.z);
  return vec3(uK0.x+cos(a)*rr, uK0.y+sin(a)*rr*0.8, 0.0); }
vec4 K(int i){ if(i==0) return uK0;
  vec3 p=orb(i,uA.y), q=orb(i,uA.y-uA.x); return vec4(p.xy,(p.xy-q.xy)/max(uA.x,1e-4)+uK0.zw); }

vec2 curl(vec2 p,float t,float caos,float U){
  vec2 v=vec2(0.0); float sc=1.0/U;
  for(int i=0;i<3;i++){
    float fi=float(i), kx=(0.0030+0.0021*fi)*sc*(1.0+0.37*fi), ky=(0.0024+0.0019*fi)*sc*(1.0-0.21*fi);
    float px=kx*p.x+1.3*fi+t*(0.11+0.05*fi), py=ky*p.y+0.7+2.1*fi-t*(0.09+0.04*fi);
    float amp=(i==0)?1.0:((i==1)?0.7*(0.4+caos):0.5*caos), n=1.0/length(vec2(kx,ky));
    v+=amp*n*vec2(ky*sin(px)*cos(py), -kx*cos(px)*sin(py));
  }
  return v;
}

void main(){
  ivec2 p=ivec2(gl_FragCoord.xy);
  int TEX=int(uTDOutputInfo.res.z);
  uint id=uint(p.y*TEX+p.x);
  vec4 s=texelFetch(sTD2DInputs[0],p,0);
  float dt=uA.x, T=uA.y, caos=uA.z, U=uA.w, R0=uB.w;
  float low=uB.x, mid=uB.y;
  uint fr=uint(uD.w);
  float exp_=uW.z, swl=uW.w;
  float spr=3.4+2.2*low, S=(70.0+80.0*mid)*U*(1.0+0.9*swl), br=1.0+1.2*exp_;
  float vein=(95.0+90.0*mid)*U*1.0, V0=430.0*U, E=R0*0.9, Gm=V0*V0*R0*0.643;
  int cl=cls(id);

  if(cl==2){                                    // HAZ
    vec2 d=s.xy-uK0.xy; float r2=dot(d,d)+E*E;
    s.zw-=d*Gm/(r2*sqrt(r2))*dt; s.zw*=exp(-dt*0.02); s.xy+=s.zw*dt;
    bool lanza=(uC.x>0.5)&&(hu(id*3u+1u)%32u==uint(uC.y));
    bool azar=hf(id*43u+fr)<dt/5.0||dot(d,d)>400.0*R0*R0||any(isnan(s));
    if(azar&&!lanza) s=vec4(uK0.xy,0.0,0.0);   // aparcada en el núcleo hasta el próximo golpe
    if(lanza){ float a=uC.z+(hf(id*31u+fr)-0.5)*0.10, sp=uC.w*(0.965+0.07*hf(id*37u+fr));
      vec2 dir=vec2(cos(a),sin(a)); s=vec4(uK0.xy+dir*E*0.35,dir*sp+uK0.zw*0.4); }
    fragColor=TDOutputSwizzle(s); return;
  }

  // el núcleo SIEMPRE ata al núcleo principal (compacto); las venas nacen también en los satélites
  int c=(cl==1)?0:((id%7u<3u)?0:int(1u+id%3u)); vec4 Kc=K(c); vec2 d=s.xy-Kc.xy; float dist=length(d)+1e-5; vec2 u=d/dist, tg=vec2(-u.y,u.x);
  float fall=1.0/(1.0+dist/R0);
  s.zw+=u*uD.x*300.0*U*fall+tg*uD.y*200.0*U*fall;

  if(cl==1){                                    // NÚCLEO
    float ir=pow(hf(id*3u+1u),3.0)*R0*1.1, ph=hf(id*5u+2u)*6.2831;
    if(uD.z>0.0&&(id&3u)==0u) s.zw+=(vec2(hf(id*11u+fr),hf(id*13u+fr))*2.0-1.0)*uD.z*200.0*U;
    float vr=clamp(-(dist-ir*br)*spr,-340.0*U,340.0*U), vt=S*(0.45+0.55*exp(-dist/R0))*(((c&1)==1)?-1.0:1.0);
    vec2 tb=vec2(sin(s.y*0.02/U+T*0.4+ph),cos(s.x*0.02/U-T*0.33+ph))*150.0*U*(0.4+caos);
    s.zw+=(u*vr+tg*vt+tb+uW.xy*0.7+Kc.zw*0.9-s.zw)*(1.0-exp(-dt*2.5)); s.xy+=s.zw*dt;
    if(dist>R0*5.0||any(isnan(s))){ float a=hf(id*17u+fr)*6.2831; s=vec4(Kc.xy+vec2(cos(a),sin(a))*ir,0.0,0.0); }
    fragColor=TDOutputSwizzle(s); return;
  }

  // VENA
  vec2 tgt=curl(s.xy,T,caos,U)*vein+u*vein*0.10+uW.xy*0.9;
  s.zw+=(tgt-s.zw)*(1.0-exp(-dt*1.6)); s.xy+=s.zw*dt;
  if(hf(id*41u+fr)<dt/2.6||dist>R0*60.0||any(isnan(s))){ float a=hf(id*17u+fr)*6.2831, r=R0*1.3*hf(id*19u+fr);
    vec2 dir=vec2(cos(a),sin(a)); s=vec4(Kc.xy+dir*r,dir*vein*(0.6+1.4*hf(id*59u+fr))); }
  fragColor=TDOutputSwizzle(s);
}
