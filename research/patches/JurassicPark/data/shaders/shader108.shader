# BBH Shader File
# 
Name: shader108
Descr: shader108 description
VertShader: include/common_matrix.gls
VertShader: shared/vert_pos_tex_clr.gls
FragShader: include/frag_bloom.gls
FragShader: shader108/frag.gls
UniformData: int uTexture0 0
UniformData: float uGlowThreshold 1
UniformData: float uGlowRamp 2
VertData: aVertex 0
VertData: aColor 1
VertData: aTexCoord0 2
