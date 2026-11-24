# Regenerates njin_rl_own.inl from the raylib sources FetchContent pinned in build/
# (docs/renderer-legacy-removal.md, step 3E). Run from the repository root:
#   python src/engine/runtime/njin_rl_own_gen.py
# USED lists the raymath functions the shared runtime calls; their callees and
# those of the copied rmodels/rtextures functions are added on their own.
import re
USED = ['MatrixDecompose', 'MatrixDeterminant', 'MatrixIdentity', 'MatrixInvert',
        'MatrixLookAt', 'MatrixMultiply', 'MatrixOrtho', 'MatrixPerspective',
        'MatrixRotate', 'MatrixRotateX', 'MatrixRotateY', 'MatrixRotateZ',
        'MatrixScale', 'MatrixToFloatV', 'MatrixTranslate', 'MatrixTranspose',
        'QuaternionLength', 'QuaternionNormalize', 'QuaternionSlerp',
        'QuaternionToMatrix', 'Vector3Add', 'Vector3CrossProduct', 'Vector3Distance',
        'Vector3DotProduct', 'Vector3Length', 'Vector3Lerp', 'Vector3Max',
        'Vector3Min', 'Vector3Negate', 'Vector3Normalize', 'Vector3RotateByAxisAngle',
        'Vector3Scale', 'Vector3Subtract', 'Vector3Transform']
raymath = open('build/_deps/raylib-src/src/raymath.h', encoding='utf-8').read()
raylib = open('build/_deps/raylib-src/src/raylib.h', encoding='utf-8').read()
rmodels = open('build/_deps/raylib-src/src/rmodels.c', encoding='utf-8').read()
rtextures = open('build/_deps/raylib-src/src/rtextures.c', encoding='utf-8').read()
used = list(USED)
model_funcs = ['GetMeshBoundingBox', 'GetModelBoundingBox', 'GetRayCollisionSphere', 'GetRayCollisionBox',
               'GetRayCollisionTriangle', 'GetRayCollisionMesh']
texture_funcs = ['IsTextureValid', 'IsRenderTextureValid', 'GetPixelDataSize']
def model_src(name, text=rmodels):
    m = re.search(r'^[A-Za-z][A-Za-z0-9 *]* ' + name + r'\(', text, re.M)
    assert m, name
    end = text.index('\n}\n', m.start()) + 3
    src = text[m.start():end]
    return 'inline ' + src
model_text = [model_src(n) for n in model_funcs]
texture_text = [model_src(n, rtextures) for n in texture_funcs]
for src in model_text:
    for dep in re.findall(r'\b((?:Vector[234]|Matrix|Quaternion)[A-Za-z0-9]+)\(', src):
        if dep not in used: used.append(dep)

def func_src(name):
    m = re.search(r'^RMAPI [^(\n]*\b' + name + r'\(', raymath, re.M)
    assert m, name
    start = m.start()
    end = raymath.index('\n}\n', start) + 3
    return raymath[start:end]

need, order, seen = list(used), [], set()
while need:
    n = need.pop()
    if n in seen: continue
    seen.add(n)
    src = func_src(n)
    for dep in re.findall(r'\b((?:Vector[234]|Matrix|Quaternion)[A-Za-z0-9]+)\(', src):
        if dep != n and dep not in seen: need.append(dep)
    order.append(n)
# Definition order: as in raymath.h (callees come first there).
order.sort(key=lambda n: re.search(r'^RMAPI [^(\n]*\b' + n + r'\(', raymath, re.M).start())
funcs = '\n'.join(func_src(n).replace('RMAPI ', 'inline ', 1) for n in order)

def enum_block(first):
    i = raylib.index(first)
    s = raylib.rindex('typedef enum {', 0, i)
    e = raylib.index('}', i)
    name_end = raylib.index(';', e)
    return re.sub(r' *//[^\n]*', '', raylib[s:name_end + 1])

pixel = enum_block('PIXELFORMAT_UNCOMPRESSED_GRAYSCALE = 1')
material = enum_block('MATERIAL_MAP_ALBEDO = 0')

models = '\n'.join(model_text)
textures = '\n'.join(texture_text)
out = f'''// njin_rl_own.inl: the raylib types, enums, raymath and model helpers Njin's shared
// runtime uses, as Njin's own (docs/renderer-legacy-removal.md, step 3E). Included by
// njin_rl.h when Njin is built without raylib (NJIN_RL_OWN), and by its test inside a
// namespace, so it has no include guard and includes nothing: njin_rl.h includes
// <cmath>, <cstdlib> and <cstring> first.
//
// Structs, enums and functions are raylib 6.0's (raylib.h, raymath.h, rtextures.c, rmodels.c; zlib licence,
// Copyright (c) 2013-2026 Ramon Santamaria; THIRD_PARTY_NOTICES.md), with the same
// layouts and the same arithmetic in the same order, so results are bit for bit those
// of raylib. Generated from the pinned sources by njin_rl_own_gen.py; altered from them in these ways: RMAPI
// is `inline`, only what Njin uses is kept, the struct comments are dropped.

#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef DEG2RAD
#define DEG2RAD (PI/180.0f)
#endif
#ifndef RAD2DEG
#define RAD2DEG (180.0f/PI)
#endif
#ifndef EPSILON
#define EPSILON 0.000001f
#endif

#if !defined(NJIN_RL_OWN_MATH)
#define NJIN_RL_OWN_MATH
typedef struct Vector2 {{ float x; float y; }} Vector2;
typedef struct Vector3 {{ float x; float y; float z; }} Vector3;
typedef struct Vector4 {{ float x; float y; float z; float w; }} Vector4;
typedef Vector4 Quaternion;
typedef struct Matrix {{
    float m0, m4, m8, m12;
    float m1, m5, m9, m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
}} Matrix;
typedef struct float3 {{ float v[3]; }} float3;
typedef struct float16 {{ float v[16]; }} float16;

{funcs}
#endif // NJIN_RL_OWN_MATH

// Everything else; njin_rlmath.h (NJIN_RL_MATH_ONLY) wants the math alone.
#if !defined(NJIN_RL_MATH_ONLY) && !defined(NJIN_RL_OWN_TYPES)
#define NJIN_RL_OWN_TYPES
typedef struct Color {{ unsigned char r; unsigned char g; unsigned char b; unsigned char a; }} Color;
typedef struct Rectangle {{ float x; float y; float width; float height; }} Rectangle;
typedef struct Image {{ void *data; int width; int height; int mipmaps; int format; }} Image;
typedef struct Texture {{ unsigned int id; int width; int height; int mipmaps; int format; }} Texture;
typedef Texture Texture2D;
typedef struct RenderTexture {{ unsigned int id; Texture texture; Texture depth; }} RenderTexture;
typedef RenderTexture RenderTexture2D;
typedef struct GlyphInfo {{ int value; int offsetX; int offsetY; int advanceX; Image image; }} GlyphInfo;
typedef struct Font {{ int baseSize; int glyphCount; int glyphPadding; Texture2D texture; Rectangle *recs; GlyphInfo *glyphs; }} Font;
typedef struct Camera3D {{ Vector3 position; Vector3 target; Vector3 up; float fovy; int projection; }} Camera3D;
typedef Camera3D Camera;
typedef struct Camera2D {{ Vector2 offset; Vector2 target; float rotation; float zoom; }} Camera2D;
typedef struct Mesh {{
    int vertexCount;
    int triangleCount;
    float *vertices;
    float *texcoords;
    float *texcoords2;
    float *normals;
    float *tangents;
    unsigned char *colors;
    unsigned short *indices;
    int boneCount;
    unsigned char *boneIndices;
    float *boneWeights;
    float *animVertices;
    float *animNormals;
    unsigned int vaoId;
    unsigned int *vboId;
}} Mesh;
typedef struct Shader {{ unsigned int id; int *locs; }} Shader;
typedef struct MaterialMap {{ Texture2D texture; Color color; float value; }} MaterialMap;
typedef struct Material {{ Shader shader; MaterialMap *maps; float params[4]; }} Material;
typedef struct Transform {{ Vector3 translation; Quaternion rotation; Vector3 scale; }} Transform;
typedef struct BoneInfo {{ char name[32]; int parent; }} BoneInfo;
typedef Transform *ModelAnimPose;
typedef struct ModelSkeleton {{ int boneCount; BoneInfo *bones; ModelAnimPose bindPose; }} ModelSkeleton;
typedef struct Model {{
    Matrix transform;
    int meshCount;
    int materialCount;
    Mesh *meshes;
    Material *materials;
    int *meshMaterial;
    ModelSkeleton skeleton;
    ModelAnimPose currentPose;
    Matrix *boneMatrices;
}} Model;
typedef struct ModelAnimation {{ char name[32]; int boneCount; int keyframeCount; ModelAnimPose *keyframePoses; }} ModelAnimation;
typedef struct Ray {{ Vector3 position; Vector3 direction; }} Ray;
typedef struct RayCollision {{ bool hit; float distance; Vector3 point; Vector3 normal; }} RayCollision;
typedef struct BoundingBox {{ Vector3 min; Vector3 max; }} BoundingBox;

{pixel}

{material}

// raylib's MemAlloc/MemRealloc/MemFree: calloc'd, realloc, free (RL_CALLOC/RL_FREE).
inline void *MemAlloc(unsigned int size) {{ return calloc(size, 1); }}
inline void *MemRealloc(void *ptr, unsigned int size) {{ return realloc(ptr, size); }}
inline void MemFree(void *ptr) {{ free(ptr); }}

// rtextures.c: texture validity and pixel data sizes (the inspector's memory view).
{textures}

// rmodels.c: bounding boxes and ray collisions, as raylib computes them.
{models}
#endif // NJIN_RL_OWN_TYPES
'''
open('src/engine/runtime/njin_rl_own.inl', 'w', newline='\n', encoding='utf-8').write(out)
print(len(order), 'functions:', ' '.join(order))
