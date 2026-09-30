"""One-time narrow engine change: opt-in SDF clay shading for Sandtable.
Run from the repository's Sandtable workspace. Asserts original anchors first.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[5]
api=root/'engine/api/njin_3d.h'
runtime=root/'engine/runtime/modules/render3d.cpp'
header=root/'engine/runtime/modules/render3d.h'

def replace(text,old,new):
    assert text.count(old)==1, old[:100]
    return text.replace(old,new)

a=api.read_text(encoding='utf-8')
a=replace(a,'  bool cast_shadows = true;  ///< Đổ bóng khi njin::light3d bật `shadows`.','''  bool cast_shadows = true;  ///< Đổ bóng khi njin::light3d bật `shadows`.
  /// Opt-in hand-shaped clay normal/albedo variation for SDF draws only.
  /// 0 keeps the original smooth surface. Does not change hit depth or silhouette.
  f32 clay = 0.0f;
  f32 clay_detail = 9.0f; ///< Grain frequency relative to the closest SDF part radius.''')
h=header.read_text(encoding='utf-8')
h=replace(h,'  // The SDF shader only.','  // The SDF shader only.\n  i32 clay_surface = -1;')
s=runtime.read_text(encoding='utf-8')
s=replace(s,'uniform int blendCount;','uniform int blendCount;\nuniform vec2 claySurface; // amount, frequency relative to each part radius')
noise='''
// Continuous value noise; texture coordinates follow the closest articulated
// capsule instead of world space, so moving a person does not swim through grain.
float clay_hash(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}
float clay_noise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(clay_hash(i), clay_hash(i+vec3(1,0,0)), f.x),
                 mix(clay_hash(i+vec3(0,1,0)), clay_hash(i+vec3(1,1,0)), f.x), f.y),
             mix(mix(clay_hash(i+vec3(0,0,1)), clay_hash(i+vec3(1,0,1)), f.x),
                 mix(clay_hash(i+vec3(0,1,1)), clay_hash(i+vec3(1)), f.x), f.y), f.z);
}
void clay_shade(vec3 p, inout vec3 n, inout vec3 albedo) {
  if (claySurface.x <= 0.0) return;
  vec3 anchor = vec3(0), axis = vec3(0,1,0);
  float radius = max(length(shapeBounds) * 0.2, 0.0001), nearest = 1e10;
  if (shapeKind == 5) {
    for (int i=0; i<blendCount; ++i) {
      float d = round_cone(p, blendA[i].xyz, blendB[i].xyz, blendA[i].w, blendB[i].w);
      if (d < nearest) {
        nearest = d;
        anchor = blendA[i].xyz;
        vec3 segment = blendB[i].xyz - anchor;
        axis = dot(segment,segment)>1e-9 ? normalize(segment) : vec3(0,1,0);
        radius = max((blendA[i].w+blendB[i].w)*0.5, 0.0001);
      }
    }
  }
  vec3 tangent = normalize(cross(axis, abs(axis.z)<0.9 ? vec3(0,0,1) : vec3(1,0,0)));
  mat3 basis = mat3(tangent, axis, cross(tangent,axis));
  vec3 q = transpose(basis) * (p-anchor) / radius * claySurface.y;
  // Fade grain below pixel resolution to avoid sparkly noise in an RTS crowd.
  float footprint = max(length(dFdx(q)),length(dFdy(q)));
  float amount = claySurface.x * (1.0-smoothstep(0.35,1.4,footprint));
  vec3 e=vec3(0.08,0,0);
  vec3 g=vec3(clay_noise(q+e.xyy)-clay_noise(q-e.xyy),
              clay_noise(q+e.yxy)-clay_noise(q-e.yxy),
              clay_noise(q+e.yyx)-clay_noise(q-e.yyx))/0.16;
  g=basis*g;
  n=normalize(n-amount*0.32*(g-n*dot(g,n)));
  albedo*=1.0+amount*0.12*(clay_noise(q*0.32)-0.5);
}

'''
s=replace(s,'vec3 sdf_normal(vec3 p, float e) {',noise+'vec3 sdf_normal(vec3 p, float e) {')
s=replace(s,'''  vec3 n = normalize(mat3(shapeToWorld) * sdf_normal(local, eps));
  finalColor = vec4(shade(colDiffuse.rgb, n, pos, cut, vec3(0.0)), colDiffuse.a);''','''  vec3 localNormal = sdf_normal(local, eps);
  vec3 albedo = colDiffuse.rgb;
  clay_shade(local, localNormal, albedo);
  vec3 n = normalize(mat3(shapeToWorld) * localNormal);
  finalColor = vec4(shade(albedo, n, pos, cut, vec3(0.0)), colDiffuse.a);''')
s=replace(s,'  l.shape_kind = loc("shapeKind");','  l.clay_surface = loc("claySurface");\n  l.shape_kind = loc("shapeKind");')
s=replace(s,'  set_vec2(sh, l.surface, {m.specular, std::max(m.shininess, 1.0f)});','  set_vec2(sh, l.surface, {m.specular, std::max(m.shininess, 1.0f)});\n  set_vec2(sh, l.clay_surface, {clamp(m.clay, 0.0f, 1.0f), std::max(m.clay_detail, 0.1f)});')
for path,text in [(api,a),(header,h),(runtime,s)]:
    path.write_text(text,encoding='utf-8')
    print(path)
