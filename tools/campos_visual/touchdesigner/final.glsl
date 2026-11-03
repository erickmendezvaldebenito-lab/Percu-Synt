// final.glsl — GLSL TOP: multiplica las tres capas (blanco = 1) → papel blanco con humo negro.
layout(location = 0) out vec4 fragColor;
void main(){
  vec3 c=vec3(1.0);
  for(int i=0;i<3;i++) c*=texture(sTD2DInputs[i],vUV.st).rgb;
  fragColor=TDOutputSwizzle(vec4(c,1.0));
}
