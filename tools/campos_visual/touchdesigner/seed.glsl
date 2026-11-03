// seed.glsl — GLSL TOP: estado inicial (partículas cerca del centro, velocidad 0). Alimenta al Feedback TOP.
layout(location = 0) out vec4 fragColor;
uint hu(uint x){ x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return x; }
float hf(uint x){ return float(hu(x))/4294967296.0; }
void main(){
  ivec2 p=ivec2(gl_FragCoord.xy); int TEX=int(uTDOutputInfo.res.z); uint id=uint(p.y*TEX+p.x);
  float a=hf(id*5u+1u)*6.2831, r=pow(hf(id*7u+2u),2.0)*0.2;
  fragColor=TDOutputSwizzle(vec4(cos(a)*r,sin(a)*r,0.0,0.0));
}
