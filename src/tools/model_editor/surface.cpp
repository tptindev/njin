#include "document.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>

namespace model_editor {
static float smooth_min(float a, float b, float k) {
  if (k < 0.00001f) return std::min(a,b);
  float h = std::clamp(0.5f + 0.5f*(b-a)/k,0.0f,1.0f);
  return b + (a-b)*h - k*h*(1-h);
}
Field::Field(const Document &doc, bool posed, const std::vector<Matrix> *matrices) {
  auto bones = matrices ? *matrices : bone_matrices(doc, posed);
  low = {1e9f,1e9f,1e9f}; high = {-1e9f,-1e9f,-1e9f};
  float padding = 0.05f;
  for (const auto &s : doc.shapes) if (s.visible) {
    Matrix m = shape_matrix(s,bones);
    shapes.push_back({s,MatrixInvert(m)});
    float extent = s.radius;
    if (s.kind == 1) extent = Vector3Length(s.size);
    if (s.kind == 2 || s.kind == 3) extent = s.height*0.5f + s.radius;
    if (s.kind == 4) extent = s.radius+s.thickness;
    Vector3 p{m.m12,m.m13,m.m14}, e{extent,extent,extent};
    low = Vector3Min(low,Vector3Subtract(p,e)); high = Vector3Max(high,Vector3Add(p,e));
    if (s.operation == 1) padding += s.blend*0.25f;
  }
  if (shapes.empty()) { low = {-1,-1,-1}; high = {1,1,1}; }
  else { low = Vector3Subtract(low,{padding,padding,padding}); high = Vector3Add(high,{padding,padding,padding}); }
}
float Field::distance(Vector3 point) const {
  float result = 1e9f; bool first = true;
  for (const auto &item : shapes) {
    const auto &s = item.shape;
    Vector3 p = Vector3Transform(point,item.inverse);
    float d = 0;
    switch (s.kind) {
    case 0: d = Vector3Length(p)-s.radius; break;
    case 1: {
      Vector3 q{std::abs(p.x)-s.size.x,std::abs(p.y)-s.size.y,std::abs(p.z)-s.size.z};
      d = Vector3Length(Vector3Max(q,{}))+std::min(std::max({q.x,q.y,q.z}),0.0f); break;
    }
    case 2: p.y -= std::clamp(p.y,-s.height*0.5f,s.height*0.5f); d = Vector3Length(p)-s.radius; break;
    case 3: {
      float a = std::hypot(p.x,p.z)-s.radius, b = std::abs(p.y)-s.height*0.5f;
      d = std::hypot(std::max(a,0.0f),std::max(b,0.0f))+std::min(std::max(a,b),0.0f); break;
    }
    case 4: d = std::hypot(std::hypot(p.x,p.z)-s.radius,p.y)-s.thickness; break;
    }
    if (first) { result = d; first = false; }
    else if (s.operation == 0) result = std::min(result,d);
    else if (s.operation == 1) result = smooth_min(result,d,s.blend);
    else if (s.operation == 2) result = std::max(result,-d);
    else result = std::max(result,d);
  }
  return result;
}
Vector3 Field::normal(Vector3 p) const {
  constexpr float e = 0.0005f;
  return Vector3Normalize({distance({p.x+e,p.y,p.z})-distance({p.x-e,p.y,p.z}),
    distance({p.x,p.y+e,p.z})-distance({p.x,p.y-e,p.z}),
    distance({p.x,p.y,p.z+e})-distance({p.x,p.y,p.z-e})});
}
Surface triangulate(const Field &f, int resolution) {
  Surface out;
  if (f.shapes.empty()) return out;
  int n = std::clamp(resolution,12,96), side = n+1;
  Vector3 step = Vector3Scale(Vector3Subtract(f.high,f.low),1.0f/n);
  auto point = [&](int x,int y,int z) { return Vector3Add(f.low,{step.x*x,step.y*y,step.z*z}); };
  auto index = [side](int x,int y,int z) { return (z*side+y)*side+x; };
  std::vector<float> values(side*side*side);
  for (int z=0;z<=n;++z) for (int y=0;y<=n;++y) for (int x=0;x<=n;++x)
    values[index(x,y,z)] = f.distance(point(x,y,z));
  auto triangle = [&](Vector3 a,Vector3 b,Vector3 c) {
    Vector3 cross = Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a));
    if (Vector3LengthSqr(cross)<1e-16f) return;
    Vector3 na=f.normal(a), nb=f.normal(b), nc=f.normal(c);
    if (Vector3DotProduct(cross,Vector3Add(Vector3Add(na,nb),nc))<0) { std::swap(b,c); std::swap(nb,nc); }
    out.vertices.insert(out.vertices.end(),{a,b,c}); out.normals.insert(out.normals.end(),{na,nb,nc});
  };
  constexpr int offsets[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
  constexpr int tetrahedra[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
  for (int z=0;z<n;++z) for (int y=0;y<n;++y) for (int x=0;x<n;++x) {
    Vector3 p[8]; float d[8]; int inside=0;
    for (int i=0;i<8;++i) {
      int xx=x+offsets[i][0], yy=y+offsets[i][1], zz=z+offsets[i][2];
      p[i]=point(xx,yy,zz); d[i]=values[index(xx,yy,zz)]; inside += d[i]<0;
    }
    if (inside==0 || inside==8) continue;
    auto edge = [&](int a,int b) { return Vector3Lerp(p[a],p[b],d[a]/(d[a]-d[b])); };
    for (const auto &t : tetrahedra) {
      int in[4], ex[4], ni=0, ne=0;
      for (int i : t) { if (d[i]<0) in[ni++]=i; else ex[ne++]=i; }
      if (ni==1) triangle(edge(in[0],ex[0]),edge(in[0],ex[1]),edge(in[0],ex[2]));
      if (ni==3) triangle(edge(ex[0],in[0]),edge(ex[0],in[1]),edge(ex[0],in[2]));
      if (ni==2) {
        auto a=edge(in[0],ex[0]),b=edge(in[0],ex[1]),c=edge(in[1],ex[0]),e=edge(in[1],ex[1]);
        triangle(a,b,c); triangle(b,e,c);
      }
    }
  }
  return out;
}
bool export_obj(const std::string &path, const Surface &s) {
  // Export positions and normals; the editable rig stays in the project JSON.
  std::ofstream f(std::filesystem::path(std::u8string(path.begin(),path.end())));
  f.imbue(std::locale::classic()); f << std::setprecision(9);
  f << "# njin SDF model editor: baked surface, +Y up\no Model\n";
  for (auto v:s.vertices) f << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  for (auto v:s.normals) f << "vn " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  for (size_t i=0;i<s.vertices.size();i+=3)
    f << "f " << i+1 << "//" << i+1 << ' ' << i+2 << "//" << i+2 << ' ' << i+3 << "//" << i+3 << '\n';
  f.flush(); return f.good();
}
}
