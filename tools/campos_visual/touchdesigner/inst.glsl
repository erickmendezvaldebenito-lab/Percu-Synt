// inst.glsl — GLSL TOP. Convierte el estado de la simulación (x, y, vx, vy) en datos de instancia: r=x  g=y  b=ángulo (grados)  a=longitud.
// Cada capa usa un filtro de clase (uCls.x): 0 vena · 1 núcleo · 2 haz. Las partículas de otra clase salen con longitud 0 (no se ven).
uniform vec4 uCls;   // x = clase que dibuja esta capa   y = factor de longitud (segundos de estela)   z = longitud mínima   w = longitud máxima
layout(location = 0) out vec4 fragColor;
uint hu(uint x){ x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return x; }
float hf(uint x){ return float(hu(x))/4294967296.0; }
int cls(uint id){ float h=hf(id*23u+7u); return h<0.004?2:(h<0.12?1:0); }
void main(){
  ivec2 p=ivec2(gl_FragCoord.xy); int TEX=int(uTDOutputInfo.res.z); uint id=uint(p.y*TEX+p.x);
  vec4 s=texelFetch(sTD2DInputs[0],p,0);
  float sp=length(s.zw);
  float len=(cls(id)==int(uCls.x+0.5))?clamp(sp*uCls.y,uCls.z,uCls.w):0.0;
  float ang=degrees(atan(s.w,s.z));
  fragColor=TDOutputSwizzle(vec4(s.xy,ang,len));
}
