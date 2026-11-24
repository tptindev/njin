// njin_rl_own.inl (Njin's own copy of the raylib types and raymath functions its shared
// runtime uses) against raylib itself, while raylib is still built: every struct has
// raylib's size and field offsets, every enum value matches, and every function gives
// bit-identical results on random inputs.
#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace own {
#include "njin_rl_own.inl"
}

#define SAME_LAYOUT(T) static_assert(sizeof(own::T) == sizeof(::T) && alignof(own::T) == alignof(::T), #T " layout")
SAME_LAYOUT(Vector2);
SAME_LAYOUT(Vector3);
SAME_LAYOUT(Vector4);
SAME_LAYOUT(Quaternion);
SAME_LAYOUT(Matrix);
SAME_LAYOUT(float16);
SAME_LAYOUT(Color);
SAME_LAYOUT(Rectangle);
SAME_LAYOUT(Image);
SAME_LAYOUT(Texture);
SAME_LAYOUT(RenderTexture);
SAME_LAYOUT(GlyphInfo);
SAME_LAYOUT(Font);
SAME_LAYOUT(Camera3D);
SAME_LAYOUT(Camera2D);
SAME_LAYOUT(Mesh);
SAME_LAYOUT(Shader);
SAME_LAYOUT(MaterialMap);
SAME_LAYOUT(Material);
SAME_LAYOUT(Transform);
SAME_LAYOUT(BoneInfo);
SAME_LAYOUT(ModelSkeleton);
SAME_LAYOUT(Model);
SAME_LAYOUT(ModelAnimation);
SAME_LAYOUT(Ray);
SAME_LAYOUT(RayCollision);
SAME_LAYOUT(BoundingBox);
#define SAME_FIELD(T, f) static_assert(offsetof(own::T, f) == offsetof(::T, f), #T "." #f)
SAME_FIELD(Matrix, m12); SAME_FIELD(Matrix, m1); SAME_FIELD(Matrix, m15);
SAME_FIELD(Mesh, vertices); SAME_FIELD(Mesh, indices); SAME_FIELD(Mesh, boneCount); SAME_FIELD(Mesh, boneWeights);
SAME_FIELD(Mesh, animNormals); SAME_FIELD(Mesh, vboId);
SAME_FIELD(Model, meshCount); SAME_FIELD(Model, meshes); SAME_FIELD(Model, meshMaterial); SAME_FIELD(Model, skeleton);
SAME_FIELD(Model, currentPose); SAME_FIELD(Model, boneMatrices);
SAME_FIELD(Font, texture); SAME_FIELD(Font, recs); SAME_FIELD(Font, glyphs);
SAME_FIELD(GlyphInfo, image); SAME_FIELD(Image, format); SAME_FIELD(Texture, format); SAME_FIELD(RenderTexture, depth);
SAME_FIELD(Material, params); SAME_FIELD(MaterialMap, value); SAME_FIELD(Transform, scale); SAME_FIELD(BoneInfo, parent);
SAME_FIELD(ModelAnimation, keyframePoses); SAME_FIELD(RayCollision, normal); SAME_FIELD(Camera3D, projection);
static_assert(own::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 == ::PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, "pixel format");
static_assert(own::PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA == ::PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA, "pixel format");
static_assert(own::MATERIAL_MAP_BRDF == ::MATERIAL_MAP_BRDF && own::MATERIAL_MAP_EMISSION == ::MATERIAL_MAP_EMISSION, "material map");

namespace {
int failures = 0, checks = 0;
std::mt19937 rng(12345);
float r(float lo = -10, float hi = 10) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
template <class A, class B> bool same(const A &a, const B &b) {
  static_assert(sizeof(A) == sizeof(B));
  return std::memcmp(&a, &b, sizeof(A)) == 0;
}
template <class To, class From> To as(const From &f) {
  static_assert(sizeof(To) == sizeof(From));
  To t;
  std::memcpy(&t, &f, sizeof(To));
  return t;
}
void expect(bool ok, const char *name) {
  ++checks;
  if (!ok && failures++ < 20) std::printf("FAIL %s\n", name);
}
::Vector3 v3() { return {r(), r(), r()}; }
::Quaternion q4() { return {r(-1, 1), r(-1, 1), r(-1, 1), r(-1, 1)}; }
::Matrix m4() {
  ::Matrix m;
  float *p = &m.m0;
  for (int i = 0; i < 16; ++i) p[i] = r(-3, 3);
  return m;
}
} // namespace

int main() {
  for (int i = 0; i < 10000; ++i) {
    const ::Vector3 a = v3(), b = v3();
    const ::Quaternion q = q4(), p = q4();
    const ::Matrix m = m4(), n = m4();
    const float s = r(), t = r(0, 1), angle = r(-3.14f, 3.14f);
    const own::Vector3 oa = as<own::Vector3>(a), ob = as<own::Vector3>(b);
    const own::Quaternion oq = as<own::Quaternion>(q), op = as<own::Quaternion>(p);
    const own::Matrix om = as<own::Matrix>(m), on = as<own::Matrix>(n);
    expect(same(own::Vector3Add(oa, ob), Vector3Add(a, b)), "Vector3Add");
    expect(same(own::Vector3Subtract(oa, ob), Vector3Subtract(a, b)), "Vector3Subtract");
    expect(same(own::Vector3Scale(oa, s), Vector3Scale(a, s)), "Vector3Scale");
    expect(same(own::Vector3CrossProduct(oa, ob), Vector3CrossProduct(a, b)), "Vector3CrossProduct");
    expect(same(own::Vector3Length(oa), Vector3Length(a)), "Vector3Length");
    expect(same(own::Vector3DotProduct(oa, ob), Vector3DotProduct(a, b)), "Vector3DotProduct");
    expect(same(own::Vector3Distance(oa, ob), Vector3Distance(a, b)), "Vector3Distance");
    expect(same(own::Vector3Negate(oa), Vector3Negate(a)), "Vector3Negate");
    expect(same(own::Vector3Normalize(oa), Vector3Normalize(a)), "Vector3Normalize");
    expect(same(own::Vector3Transform(oa, om), Vector3Transform(a, m)), "Vector3Transform");
    expect(same(own::Vector3RotateByAxisAngle(oa, ob, angle), Vector3RotateByAxisAngle(a, b, angle)), "Vector3RotateByAxisAngle");
    expect(same(own::Vector3Lerp(oa, ob, t), Vector3Lerp(a, b, t)), "Vector3Lerp");
    expect(same(own::Vector3Min(oa, ob), Vector3Min(a, b)), "Vector3Min");
    expect(same(own::Vector3Max(oa, ob), Vector3Max(a, b)), "Vector3Max");
    expect(same(own::MatrixDeterminant(om), MatrixDeterminant(m)), "MatrixDeterminant");
    expect(same(own::MatrixTranspose(om), MatrixTranspose(m)), "MatrixTranspose");
    expect(same(own::MatrixInvert(om), MatrixInvert(m)), "MatrixInvert");
    expect(same(own::MatrixIdentity(), MatrixIdentity()), "MatrixIdentity");
    expect(same(own::MatrixMultiply(om, on), MatrixMultiply(m, n)), "MatrixMultiply");
    expect(same(own::MatrixTranslate(a.x, a.y, a.z), MatrixTranslate(a.x, a.y, a.z)), "MatrixTranslate");
    expect(same(own::MatrixRotate(ob, angle), MatrixRotate(b, angle)), "MatrixRotate");
    expect(same(own::MatrixRotateX(angle), MatrixRotateX(angle)), "MatrixRotateX");
    expect(same(own::MatrixRotateY(angle), MatrixRotateY(angle)), "MatrixRotateY");
    expect(same(own::MatrixRotateZ(angle), MatrixRotateZ(angle)), "MatrixRotateZ");
    expect(same(own::MatrixScale(a.x, a.y, a.z), MatrixScale(a.x, a.y, a.z)), "MatrixScale");
    expect(same(own::MatrixFrustum(-1, 1, -s, s, 0.1, 100), MatrixFrustum(-1, 1, -s, s, 0.1, 100)), "MatrixFrustum");
    expect(same(own::MatrixPerspective(angle, 1.5, 0.1, 500), MatrixPerspective(angle, 1.5, 0.1, 500)), "MatrixPerspective");
    expect(same(own::MatrixOrtho(-s, s, -2, 2, 0.1, 50), MatrixOrtho(-s, s, -2, 2, 0.1, 50)), "MatrixOrtho");
    expect(same(own::MatrixLookAt(oa, ob, own::Vector3{0, 1, 0}), MatrixLookAt(a, b, ::Vector3{0, 1, 0})), "MatrixLookAt");
    expect(same(own::MatrixToFloatV(om), MatrixToFloatV(m)), "MatrixToFloatV");
    expect(same(own::QuaternionLength(oq), QuaternionLength(q)), "QuaternionLength");
    expect(same(own::QuaternionNormalize(oq), QuaternionNormalize(q)), "QuaternionNormalize");
    expect(same(own::QuaternionLerp(oq, op, t), QuaternionLerp(q, p, t)), "QuaternionLerp");
    expect(same(own::QuaternionNlerp(oq, op, t), QuaternionNlerp(q, p, t)), "QuaternionNlerp");
    expect(same(own::QuaternionSlerp(oq, op, t), QuaternionSlerp(q, p, t)), "QuaternionSlerp");
    expect(same(own::QuaternionFromMatrix(om), QuaternionFromMatrix(m)), "QuaternionFromMatrix");
    expect(same(own::QuaternionToMatrix(oq), QuaternionToMatrix(q)), "QuaternionToMatrix");
    // A real transform to decompose: scale, rotation, translation.
    const ::Matrix trs = MatrixMultiply(MatrixMultiply(MatrixScale(1 + t, 2, 0.5f), QuaternionToMatrix(QuaternionNormalize(q))), MatrixTranslate(a.x, a.y, a.z));
    ::Vector3 t1, s1;
    ::Quaternion r1;
    own::Vector3 t2, s2;
    own::Quaternion r2;
    MatrixDecompose(trs, &t1, &r1, &s1);
    own::MatrixDecompose(as<own::Matrix>(trs), &t2, &r2, &s2);
    expect(same(t1, t2) && same(r1, r2) && same(s1, s2), "MatrixDecompose");
  }
  // rmodels.c helpers: bounding boxes and ray collisions on random meshes, indexed and not.
  for (int i = 0; i < 300; ++i) {
    const int triangles = 1 + int(r(0, 40));
    std::vector<float> vertices(size_t(triangles) * 9);
    for (auto &v : vertices) v = r(-4, 4);
    std::vector<unsigned short> indices(size_t(triangles) * 3);
    for (size_t k = 0; k < indices.size(); ++k) indices[k] = (unsigned short)(size_t(r(0, 0.999f) * float(triangles * 3)));
    ::Mesh mesh{};
    mesh.vertexCount = triangles * 3;
    mesh.triangleCount = triangles;
    mesh.vertices = vertices.data();
    if (i % 2) mesh.indices = indices.data();
    ::Model model{};
    model.meshCount = 1;
    model.meshes = &mesh;
    model.transform = MatrixMultiply(QuaternionToMatrix(QuaternionNormalize(q4())), MatrixTranslate(r(), r(), r()));
    const own::Mesh omesh = as<own::Mesh>(mesh);
    const own::Model omodel = as<own::Model>(model);
    expect(same(own::GetMeshBoundingBox(omesh), GetMeshBoundingBox(mesh)), "GetMeshBoundingBox");
    expect(same(own::GetModelBoundingBox(omodel), GetModelBoundingBox(model)), "GetModelBoundingBox");
    for (int k = 0; k < 20; ++k) {
      const ::Ray ray{v3(), Vector3Normalize(v3())};
      const own::Ray oray = as<own::Ray>(ray);
      const ::BoundingBox box{Vector3Min(v3(), v3()), Vector3Max(v3(), v3())};
      const ::Vector3 c = v3(), p1 = v3(), p2 = v3(), p3 = v3();
      const float radius = r(0.1f, 5);
      const ::Matrix transform = model.transform;
      expect(same(own::GetRayCollisionSphere(oray, as<own::Vector3>(c), radius), GetRayCollisionSphere(ray, c, radius)), "GetRayCollisionSphere");
      expect(same(own::GetRayCollisionBox(oray, as<own::BoundingBox>(box)), GetRayCollisionBox(ray, box)), "GetRayCollisionBox");
      expect(same(own::GetRayCollisionTriangle(oray, as<own::Vector3>(p1), as<own::Vector3>(p2), as<own::Vector3>(p3)),
                  GetRayCollisionTriangle(ray, p1, p2, p3)), "GetRayCollisionTriangle");
      expect(same(own::GetRayCollisionMesh(oray, omesh, as<own::Matrix>(transform)), GetRayCollisionMesh(ray, mesh, transform)), "GetRayCollisionMesh");
    }
  }
  // rtextures.c helpers: every pixel format at small and odd sizes, and texture validity.
  for (int format = 0; format <= ::PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA + 1; ++format)
    for (int w : {0, 1, 2, 3, 4, 5, 17, 256, 1023})
      for (int h : {0, 1, 3, 4, 9, 512})
        expect(own::GetPixelDataSize(w, h, format) == ::GetPixelDataSize(w, h, format), "GetPixelDataSize");
  for (int i = 0; i < 2000; ++i) {
    const auto field = [] { return (int)(rng() % 3) - 1; }; // -1, 0 or 1
    ::RenderTexture2D t{};
    t.id = (unsigned)(rng() % 2);
    for (::Texture2D *tex : {&t.texture, &t.depth}) {
      tex->id = (unsigned)(rng() % 2);
      tex->width = field();
      tex->height = field();
      tex->format = field();
      tex->mipmaps = field();
    }
    expect(own::IsTextureValid(as<own::Texture2D>(t.texture)) == ::IsTextureValid(t.texture), "IsTextureValid");
    expect(own::IsRenderTextureValid(as<own::RenderTexture2D>(t)) == ::IsRenderTextureValid(t), "IsRenderTextureValid");
  }
  std::printf("rl_own: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
