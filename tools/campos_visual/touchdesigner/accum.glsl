// accum.glsl — GLSL TOP: acumula el dibujo con persistencia sobre papel blanco.
//   entrada 0 = Render TOP (fondo blanco, partículas negras)   entrada 1 = Feedback TOP de esta misma capa
uniform vec4 uF;   // x = fracción que se aclara por cuadro (1-exp(-dt/tau))   y = oscuridad añadida por partícula
layout(location = 0) out vec4 fragColor;
void main(){
  vec4 prev=texture(sTD2DInputs[1],vUV.st);
  float cov=1.0-texture(sTD2DInputs[0],vUV.st).r;      // 0 = papel, 1 = partícula negra
  float v=prev.r+(1.0-prev.r)*uF.x;                    // el papel se aclara
  v*=1.0-clamp(cov*uF.y,0.0,1.0);                      // y las partículas oscurecen
  fragColor=TDOutputSwizzle(vec4(vec3(v),1.0));
}
