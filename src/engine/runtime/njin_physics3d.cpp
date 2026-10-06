// 3D physics (njin_physics3d.h) on Jolt Physics (MIT, pulled by the root
// CMakeLists). Two object layers: static bodies, and everything that moves
// (kinematic and dynamic bodies, characters); static bodies never test
// against each other. Characters are Jolt's CharacterVirtual: not bodies of
// the world, moved by velocity with ExtendedUpdate (slides along walls,
// sticks to the floor, walks up steps). The job system is single-threaded:
// a small game's handful of bodies does not pay for threads, and the step
// stays deterministic.
//
// Contacts (physics3d_contact): a ContactListener counts the touching
// sub-shape pairs of every two bodies and reports a pair when its count
// leaves or returns to 0; characters are not bodies of the world, so after
// each step their shape is collided against the world and the set of bodies
// it touches is compared with the step before. The body3d and character3d
// components are synced around the step, and their on_destroy frees the
// body.
#include "njin_physics3d_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Geometry/RayAABox.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/RegisterTypes.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace njin {
namespace {
namespace layers {
constexpr JPH::ObjectLayer still = 0;
constexpr JPH::ObjectLayer moving = 1;
constexpr JPH::uint count = 2;
} // namespace layers

class broad_phase_layers final : public JPH::BroadPhaseLayerInterface {
public:
  JPH::uint GetNumBroadPhaseLayers() const override { return layers::count; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return JPH::BroadPhaseLayer((JPH::BroadPhaseLayer::Type)layer);
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return (JPH::BroadPhaseLayer::Type)layer == layers::still ? "still" : "moving";
  }
#endif
};

// Static bodies only need to meet moving ones.
class object_vs_broad_phase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
    return layer != layers::still || (JPH::BroadPhaseLayer::Type)broad == layers::moving;
  }
};

class object_pairs final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a != layers::still || b != layers::still;
  }
};

void trace(const char *fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  NJIN_INFO("jolt: %s", buf);
}

#ifdef JPH_ENABLE_ASSERTS
// Logged, never a breakpoint: a game without a debugger attached keeps going.
bool assert_failed(const char *expr, const char *message, const char *file, JPH::uint line) {
  NJIN_WARN("jolt: assert %s (%s) at %s:%u", expr, message != nullptr ? message : "", file, line);
  return false;
}
#endif

// Jolt's type registry is process-wide: the first world sets it up, the last
// one tears it down.
i32 jolt_users = 0;

void jolt_acquire() {
  if (jolt_users++ > 0)
    return;
  JPH::RegisterDefaultAllocator();
  JPH::Trace = trace;
  JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assert_failed;)
  JPH::Factory::sInstance = new JPH::Factory();
  JPH::RegisterTypes();
}

void jolt_release() {
  if (--jolt_users > 0)
    return;
  JPH::UnregisterTypes();
  delete JPH::Factory::sInstance;
  JPH::Factory::sInstance = nullptr;
}

JPH::Vec3 jv(vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
vec3 nv(JPH::Vec3Arg v) { return vec3{v.GetX(), v.GetY(), v.GetZ()}; }

// njin's rotation order (njin::transform3d): z, then x, then y.
JPH::Quat quat_of(vec3 degrees) {
  const vec3 r = degrees * (pi / 180.0f);
  return JPH::Quat::sRotation(JPH::Vec3::sAxisY(), r.y) * JPH::Quat::sRotation(JPH::Vec3::sAxisX(), r.x) *
         JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), r.z);
}

// Back to degrees, from R = Ry * Rx * Rz: R(1,2) = -sin x, R(0,2) = sin y cos x,
// R(2,2) = cos y cos x, R(1,0) = cos x sin z, R(1,1) = cos x cos z.
vec3 degrees_of(JPH::QuatArg q) {
  const JPH::Mat44 m = JPH::Mat44::sRotation(q);
  const f32 x = std::asin(clamp(-m(1, 2), -1.0f, 1.0f));
  const f32 y = std::atan2(m(0, 2), m(2, 2));
  const f32 z = std::atan2(m(1, 0), m(1, 1));
  return vec3{x, y, z} * (180.0f / pi);
}

struct body_slot {
  JPH::BodyID id;
  bool alive = false;
  bool kinematic = false;
  bool dynamic = false;
  u64 user = 0;
  bool ragdoll = false; // a part of a ragdoll: the ragdoll owns the Jolt body
  bool has_target = false;
  vec3 target_pos{};
  vec3 target_rot{};
};

struct character_slot {
  JPH::Ref<JPH::CharacterVirtual> character;
  bool alive = false;
  bool active = true; // character3d_set_active
  vec3 desired{};
  f32 step_height = 0.3f;
  f32 mass = 70.0f; // character3d_desc::mass: its weight on what it stands on
  std::vector<u32> touching; // body handles it touched after the last step, sorted
};

struct joint_slot {
  JPH::Ref<JPH::TwoBodyConstraint> constraint;
  bool alive = false;
  joint3d_kind kind = joint3d_hinge;
  body3d_handle a{};
  body3d_handle b{};
};

// A ragdoll (ragdoll3d_create): Jolt's Ragdoll owns the bodies and their
// constraints; each body also has a body slot so raycasts, contacts and
// body3d_* see it. Model bones without a part follow `anchor`, the part
// nearest above them, keeping the offset they had at the start.
struct ragdoll_slot {
  JPH::Ref<JPH::Ragdoll> ragdoll;
  bool alive = false;
  std::vector<u32> parts;      // body handle per ragdoll3d_desc::bones entry
  std::vector<i32> part_bone;  // model bone per entry
  std::vector<i32> anchor;     // per model bone: the entry it follows
  std::vector<JPH::Mat44> rel; // per model bone: anchor's model frame -> the bone's
  f32 scale = 1.0f;            // ragdoll3d_desc::transform scale
  std::vector<shape3d> shapes; // per entry: its shape in the body's frame (ragdoll3d_shape)
};

// Two bodies touching: how many of their sub-shape pairs do, in the order the
// listener first reported them.
struct pair_state {
  u32 count = 0;
  body3d_handle a{};
  body3d_handle b{};
  bool sensor = false;
};

u64 pair_key(JPH::BodyID a, JPH::BodyID b) {
  u32 x = a.GetIndexAndSequenceNumber();
  u32 y = b.GetIndexAndSequenceNumber();
  if (x > y)
    std::swap(x, y);
  return ((u64)x << 32) | (u64)y;
}

vec3 world_vec(JPH::RVec3Arg v) { return vec3{(f32)v.GetX(), (f32)v.GetY(), (f32)v.GetZ()}; }

// A shape for a body: the same sizes as njin::shape3d draws.
JPH::RefConst<JPH::Shape> make_shape(const body3d_desc &d) {
  constexpr f32 min_half = 0.01f;
  JPH::ShapeSettings::ShapeResult result;
  switch (d.shape) {
  case shape3d_box: {
    const vec3 h{std::max(d.size.x * 0.5f, min_half), std::max(d.size.y * 0.5f, min_half),
                 std::max(d.size.z * 0.5f, min_half)};
    const f32 convex = std::min(JPH::cDefaultConvexRadius, std::min(h.x, std::min(h.y, h.z)) * 0.5f);
    result = JPH::BoxShapeSettings(jv(h), convex).Create();
    break;
  }
  case shape3d_sphere:
    result = JPH::SphereShapeSettings(std::max(d.radius, min_half)).Create();
    break;
  case shape3d_capsule: {
    const f32 r = std::max(d.radius, min_half);
    result = JPH::CapsuleShapeSettings(std::max(d.height * 0.5f - r, min_half), r).Create();
    break;
  }
  case shape3d_cylinder: {
    const f32 r = std::max(d.radius, min_half);
    const f32 half = std::max(d.height * 0.5f, min_half);
    result = JPH::CylinderShapeSettings(half, r, std::min(JPH::cDefaultConvexRadius, std::min(r, half) * 0.5f))
                 .Create();
    break;
  }
  default:
    NJIN_WARN("physics3d: shape %d has no body (only box, sphere, capsule, cylinder)", (int)d.shape);
    return nullptr;
  }
  if (result.HasError()) {
    NJIN_WARN("physics3d: shape: %s", result.GetError().c_str());
    return nullptr;
  }
  return result.Get();
}

// A shape from a model's triangles, placed as draw_model draws it (the file's
// own transform, then `scale`): every triangle for a static or kinematic
// body, the convex hull of the vertices for a dynamic one (Jolt cannot
// simulate a moving triangle mesh).
JPH::RefConst<JPH::Shape> make_model_shape(const context &ctx, const body3d_desc &d) {
  const model_slot *slot = model_slot_of(ctx.model, d.model);
  if (slot == nullptr) {
    NJIN_WARN("physics3d: body3d_desc::model is not a loaded model");
    return nullptr;
  }
  const Model &model = slot->model;
  const Matrix xf = MatrixMultiply(model.transform, MatrixScale(d.scale.x, d.scale.y, d.scale.z));
  JPH::VertexList vertices;
  JPH::IndexedTriangleList triangles;
  for (i32 m = 0; m < model.meshCount; m++) {
    const Mesh &mesh = model.meshes[m];
    if (mesh.vertices == nullptr)
      continue;
    const u32 base = (u32)vertices.size();
    for (i32 v = 0; v < mesh.vertexCount; v++) {
      const Vector3 p = Vector3Transform({mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]}, xf);
      vertices.push_back(JPH::Float3(p.x, p.y, p.z));
    }
    for (i32 t = 0; t < mesh.triangleCount; t++) {
      u32 i[3];
      for (i32 k = 0; k < 3; k++)
        i[k] = base + (mesh.indices != nullptr ? (u32)mesh.indices[t * 3 + k] : (u32)(t * 3 + k));
      triangles.push_back(JPH::IndexedTriangle(i[0], i[1], i[2]));
    }
  }
  if (triangles.empty()) {
    NJIN_WARN("physics3d: the model has no triangles for a body");
    return nullptr;
  }
  JPH::ShapeSettings::ShapeResult result;
  if (d.motion == body3d_dynamic) {
    JPH::Array<JPH::Vec3> points;
    points.reserve(vertices.size());
    for (const JPH::Float3 &v : vertices)
      points.push_back(JPH::Vec3(v));
    result = JPH::ConvexHullShapeSettings(points).Create();
  } else {
    result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
  }
  if (result.HasError()) {
    NJIN_WARN("physics3d: model shape: %s", result.GetError().c_str());
    return nullptr;
  }
  return result.Get();
}
} // namespace

struct physics3d_world;

namespace {
// Characters against each other (Jolt leaves this to the application): each
// is tested only against the ones in its own cell of a grid on x and z and
// the cells round it, not against every other, so a crowd of hundreds costs
// little more than a handful. The grid is filled at the start of each step;
// its cells are wide enough that a character cannot leave its neighbours'
// cells within the step. As Jolt's own CharacterVsCharacterCollisionSimple
// otherwise, whose tests it repeats.
class character_grid final : public JPH::CharacterVsCharacterCollision {
public:
  std::vector<JPH::CharacterVirtual *> all;
  // character3d_desc::push: such pairs do not block, they are parted after the
  // step (part()); their radius and mass for it.
  struct pusher {
    f32 radius, mass;
  };
  std::unordered_map<const JPH::CharacterVirtual *, pusher> pushers;
  bool both_push(const JPH::CharacterVirtual *a, const JPH::CharacterVirtual *b) const {
    return pushers.count(a) != 0 && pushers.count(b) != 0;
  }

  // Parts every overlapping pair of pushers by the minimum translation on x
  // and z, shared by mass; `passes` times, as one parting may make another.
  void part(i32 passes) {
    for (i32 k = 0; k < passes; k++) {
      bool any = false;
      for (JPH::CharacterVirtual *a : all) {
        const auto ia = pushers.find(a);
        if (ia == pushers.end())
          continue;
        const JPH::RVec3 pa = a->GetPosition();
        near((f32)pa.GetX(), (f32)pa.GetZ(), [&](const JPH::CharacterVirtual *cb) {
          if (cb <= a) // each pair once
            return;
          const auto ib = pushers.find(cb);
          if (ib == pushers.end())
            return;
          JPH::CharacterVirtual *b = const_cast<JPH::CharacterVirtual *>(cb);
          const JPH::RVec3 pb = b->GetPosition();
          if (std::fabs((f32)(pa.GetY() - pb.GetY())) > 1.5f)
            return; // a floor apart
          f32 dx = (f32)(pb.GetX() - pa.GetX()), dz = (f32)(pb.GetZ() - pa.GetZ());
          const f32 reach = ia->second.radius + ib->second.radius;
          const f32 d2 = dx * dx + dz * dz;
          if (d2 >= reach * reach)
            return;
          f32 d = std::sqrt(d2);
          if (d < 1e-4f) { // on top of each other: apart along x
            dx = 1.0f;
            dz = 0.0f;
            d = 1.0f;
          }
          const f32 overlap = reach - std::sqrt(d2);
          const f32 ma = ia->second.mass, mb = ib->second.mass;
          const f32 share_a = mb / (ma + mb), share_b = ma / (ma + mb);
          const JPH::Vec3 n(dx / d, 0.0f, dz / d);
          a->SetPosition(a->GetPosition() - JPH::RVec3(n * (overlap * share_a)));
          b->SetPosition(pb + JPH::RVec3(n * (overlap * share_b)));
          any = true;
        });
      }
      if (!any)
        break;
    }
  }

  void add(JPH::CharacterVirtual *c) { all.push_back(c); }
  void remove(const JPH::CharacterVirtual *c) { all.erase(std::remove(all.begin(), all.end(), c), all.end()); }
  void forget(const JPH::CharacterVirtual *c) {
    remove(c);
    pushers.erase(c);
  }
  // A character switched off (character3d_set_active) is out of the grid:
  // nothing collides with it.
  void set_active(JPH::CharacterVirtual *c, bool on) {
    remove(c);
    if (on)
      add(c);
  }

  // Sorts the characters into cells, for a step of `dt` seconds.
  void rebuild(f32 dt) {
    cells.clear();
    f32 reach = 0.05f, speed = 0.0f;
    for (const JPH::CharacterVirtual *c : all) {
      const JPH::AABox b = c->GetShape()->GetLocalBounds();
      reach = std::max(reach, b.GetExtent().GetX() + c->GetCharacterPadding());
      reach = std::max(reach, b.GetExtent().GetZ() + c->GetCharacterPadding());
      speed = std::max(speed, c->GetLinearVelocity().Length());
    }
    // Two characters touch within twice the widest reach; each may move a
    // step's worth (and a margin for the stair walk and floor snap).
    cell = 2.0f * reach + 2.0f * speed * dt + 0.1f;
    for (JPH::CharacterVirtual *c : all) {
      const JPH::RVec3 p = c->GetPosition();
      cells[key(cell_of((f32)p.GetX()), cell_of((f32)p.GetZ()))].push_back(c);
    }
  }

  void CollideCharacter(const JPH::CharacterVirtual *inCharacter, JPH::RMat44Arg inCenterOfMassTransform,
                        const JPH::CollideShapeSettings &inCollideShapeSettings, JPH::RVec3Arg inBaseOffset,
                        JPH::CollideShapeCollector &ioCollector) const override {
    const JPH::Mat44 transform1 = inCenterOfMassTransform.PostTranslated(-inBaseOffset).ToMat44();
    const JPH::Shape *shape1 = inCharacter->GetShape();
    JPH::CollideShapeSettings settings = inCollideShapeSettings;
    const JPH::AABox bounds1 = shape1->GetWorldSpaceBounds(transform1, JPH::Vec3::sOne());
    const JPH::RVec3 at = inCenterOfMassTransform.GetTranslation();
    near((f32)at.GetX(), (f32)at.GetZ(), [&](const JPH::CharacterVirtual *c) {
      if (c == inCharacter || ioCollector.ShouldEarlyOut() || both_push(inCharacter, c))
        return;
      const JPH::Mat44 transform2 = c->GetCenterOfMassTransform().PostTranslated(-inBaseOffset).ToMat44();
      settings.mMaxSeparationDistance = inCollideShapeSettings.mMaxSeparationDistance + c->GetCharacterPadding();
      const JPH::Shape *shape2 = c->GetShape();
      JPH::AABox bounds2 = shape2->GetWorldSpaceBounds(transform2, JPH::Vec3::sOne());
      bounds2.ExpandBy(JPH::Vec3::sReplicate(settings.mMaxSeparationDistance));
      if (!bounds1.Overlaps(bounds2))
        return;
      ioCollector.SetUserData(reinterpret_cast<JPH::uint64>(c));
      JPH::CollisionDispatch::sCollideShapeVsShape(shape1, shape2, JPH::Vec3::sOne(), JPH::Vec3::sOne(), transform1,
                                                   transform2, JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(),
                                                   settings, ioCollector);
    });
    ioCollector.SetUserData(0);
  }

  void CastCharacter(const JPH::CharacterVirtual *inCharacter, JPH::RMat44Arg inCenterOfMassTransform,
                     JPH::Vec3Arg inDirection, const JPH::ShapeCastSettings &inShapeCastSettings,
                     JPH::RVec3Arg inBaseOffset, JPH::CastShapeCollector &ioCollector) const override {
    const JPH::Mat44 transform1 = inCenterOfMassTransform.PostTranslated(-inBaseOffset).ToMat44();
    const JPH::ShapeCast shape_cast(inCharacter->GetShape(), JPH::Vec3::sOne(), transform1, inDirection);
    const JPH::Vec3 origin = shape_cast.mShapeWorldBounds.GetCenter();
    const JPH::Vec3 extents =
        shape_cast.mShapeWorldBounds.GetExtent() + JPH::Vec3::sReplicate(inShapeCastSettings.mExtraConvexRadius);
    JPH::ShapeCastSettings cast_settings = inShapeCastSettings;
    const JPH::RVec3 at = inCenterOfMassTransform.GetTranslation();
    near((f32)at.GetX(), (f32)at.GetZ(), [&](const JPH::CharacterVirtual *c) {
      if (c == inCharacter || ioCollector.ShouldEarlyOut() || both_push(inCharacter, c))
        return;
      const JPH::Mat44 transform2 = c->GetCenterOfMassTransform().PostTranslated(-inBaseOffset).ToMat44();
      cast_settings.mExtraConvexRadius = inShapeCastSettings.mExtraConvexRadius + c->GetCharacterPadding();
      const JPH::Shape *shape2 = c->GetShape();
      JPH::AABox bounds2 = shape2->GetWorldSpaceBounds(transform2, JPH::Vec3::sOne());
      bounds2.ExpandBy(extents + JPH::Vec3::sReplicate(c->GetCharacterPadding()));
      if (!JPH::RayAABoxHits(origin, inDirection, bounds2.mMin, bounds2.mMax))
        return;
      ioCollector.SetUserData(reinterpret_cast<JPH::uint64>(c));
      JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(shape_cast, cast_settings, shape2, JPH::Vec3::sOne(), {},
                                                          transform2, JPH::SubShapeIDCreator(),
                                                          JPH::SubShapeIDCreator(), ioCollector);
    });
    ioCollector.SetUserData(0);
  }

private:
  f32 cell = 1.0f;
  std::unordered_map<u64, std::vector<JPH::CharacterVirtual *>> cells;

  i32 cell_of(f32 v) const { return (i32)std::floor(v / cell); }
  static u64 key(i32 x, i32 z) { return ((u64)(u32)x << 32) | (u64)(u32)z; }

  // Every character in the cell of (x, z) and the eight round it.
  template <typename Fn> void near(f32 x, f32 z, Fn &&fn) const {
    const i32 cx = cell_of(x), cz = cell_of(z);
    for (i32 dz = -1; dz <= 1; dz++)
      for (i32 dx = -1; dx <= 1; dx++) {
        const auto it = cells.find(key(cx + dx, cz + dz));
        if (it != cells.end())
          for (const JPH::CharacterVirtual *c : it->second)
            fn(c);
      }
  }
};

// Body pairs starting and stopping to touch, from Jolt's contact callbacks
// (on the physics step, single-threaded here).
class contact_listener final : public JPH::ContactListener {
public:
  physics3d_world *world = nullptr;
  void OnContactAdded(const JPH::Body &body1, const JPH::Body &body2, const JPH::ContactManifold &manifold,
                      JPH::ContactSettings &) override;
  void OnContactRemoved(const JPH::SubShapeIDPair &pair) override;
};
} // namespace

// What a character stands on is not pushed aside by it: a capsule resting on
// two boards' edges would otherwise shove them apart every step (wedged in the
// gap), and the character would drop and be thrown up as they moved. What it
// walks into is still pushed, as before.
class character_listener final : public JPH::CharacterContactListener {
public:
  static bool supports(const JPH::CharacterVirtual *c, const JPH::CharacterContact &k) {
    return k.mSurfaceNormal.Dot(c->GetUp()) > 0.0f && !c->IsSlopeTooSteep(k.mSurfaceNormal);
  }
  void OnContactAdded(const JPH::CharacterVirtual *c, const JPH::CharacterContact &k,
                      JPH::CharacterContactSettings &io) override {
    if (supports(c, k))
      io.mCanReceiveImpulses = false;
  }
  void OnContactPersisted(const JPH::CharacterVirtual *c, const JPH::CharacterContact &k,
                          JPH::CharacterContactSettings &io) override {
    if (supports(c, k))
      io.mCanReceiveImpulses = false;
  }
};

struct physics3d_world {
  broad_phase_layers broad_phase;
  object_vs_broad_phase object_vs_broad;
  object_pairs pairs;
  JPH::TempAllocatorImpl temp{10 * 1024 * 1024};
  JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
  JPH::PhysicsSystem system;
  contact_listener listener;
  character_listener characters_listener;
  std::vector<body_slot> bodies;         // handle id N is bodies[N - 1]
  std::vector<character_slot> characters; // handle id N is characters[N - 1]
  std::vector<joint_slot> joints;         // handle id N is joints[N - 1]
  std::vector<ragdoll_slot> ragdolls;     // handle id N is ragdolls[N - 1]
  std::vector<JPH::RefConst<JPH::Shape>> hulls; // handle id N is hulls[N - 1], null once destroyed
  std::unordered_map<u32, u32> handle_by_body; // Jolt body id -> njin handle id
  std::unordered_map<u64, pair_state> touching; // pair_key -> the pair
  std::vector<contact3d> contacts; // events of the last step
  character_grid crowd;            // characters against each other
  // Static bodies added since the broad phase was last rebuilt: a town's
  // worth added one by one leaves it slow to query until it is.
  u32 static_added = 0;
  // Dynamic bodies a character stands on this step, with their own mass and
  // inertia, put back after the solve (physics3d_step).
  struct ridden {
    JPH::BodyID id;
    f32 inv_mass;
    JPH::Vec3 inv_inertia;
    JPH::Quat inertia_rotation;
  };
  std::vector<ridden> ridden_bodies;

  physics3d_world() {
    system.Init(16384, 0, 16384, 8192, broad_phase, object_vs_broad, pairs);
    listener.world = this;
    system.SetContactListener(&listener);
  }
  ~physics3d_world() {
    crowd.all.clear();
    characters.clear();
    for (joint_slot &j : joints)
      if (j.alive)
        system.RemoveConstraint(j.constraint);
    joints.clear();
    // A ragdoll destroys its own bodies.
    for (ragdoll_slot &r : ragdolls)
      if (r.alive) {
        r.ragdoll->RemoveFromPhysicsSystem();
        for (u32 h : r.parts)
          bodies[h - 1] = body_slot{};
      }
    ragdolls.clear();
    JPH::BodyInterface &bi = system.GetBodyInterface();
    for (body_slot &b : bodies) {
      if (b.alive) {
        bi.RemoveBody(b.id);
        bi.DestroyBody(b.id);
      }
    }
  }
};

namespace {
body3d_handle handle_by_id(const physics3d_world &w, JPH::BodyID id) {
  const auto it = w.handle_by_body.find(id.GetIndexAndSequenceNumber());
  return it != w.handle_by_body.end() ? body3d_handle{it->second} : body3d_handle{};
}

void contact_listener::OnContactAdded(const JPH::Body &body1, const JPH::Body &body2,
                                      const JPH::ContactManifold &manifold, JPH::ContactSettings &) {
  pair_state &p = world->touching[pair_key(body1.GetID(), body2.GetID())];
  if (p.count++ > 0)
    return;
  p.a = handle_by_id(*world, body1.GetID());
  p.b = handle_by_id(*world, body2.GetID());
  p.sensor = body1.IsSensor() || body2.IsSensor();
  world->contacts.push_back(contact3d{.a = p.a,
                                      .b = p.b,
                                      .character = {},
                                      .began = true,
                                      .sensor = p.sensor,
                                      .point = world_vec(manifold.GetWorldSpaceContactPointOn1(0)),
                                      .normal = nv(manifold.mWorldSpaceNormal)});
}

void contact_listener::OnContactRemoved(const JPH::SubShapeIDPair &pair) {
  const auto it = world->touching.find(pair_key(pair.GetBody1ID(), pair.GetBody2ID()));
  if (it == world->touching.end() || --it->second.count > 0)
    return;
  const pair_state p = it->second;
  world->touching.erase(it);
  world->contacts.push_back(contact3d{.a = p.a, .b = p.b, .character = {}, .began = false, .sensor = p.sensor,
                                      .point = {}, .normal = {}});
}

void on_body_destroyed(context &ctx, entt::registry &reg, entt::entity e) {
  body3d_destroy(ctx, reg.get<body3d>(e).handle);
}

void on_character_destroyed(context &ctx, entt::registry &reg, entt::entity e) {
  character3d_destroy(ctx, reg.get<character3d>(e).handle);
}

// Jolt's globals come before the world and go after it.
physics3d_world &world_of(context &ctx) {
  physics3d_state &s = ctx.physics3d;
  if (!s.world) {
    jolt_acquire();
    s.world = std::make_unique<physics3d_world>();
    s.world->system.SetGravity(jv(s.gravity));
    // A component's body goes with it (emplacing it needs a body, so the world
    // is always there first).
    entt::registry &reg = world(ctx);
    reg.on_destroy<body3d>().connect<&on_body_destroyed>(ctx);
    reg.on_destroy<character3d>().connect<&on_character_destroyed>(ctx);
  }
  return *s.world;
}

// Bodies the character's shape touches (within a small gap, so the floor it
// stands on counts), including sensors, compared with the step before.
void character_contacts(physics3d_world &w, character_slot &c, u32 handle) {
  JPH::CollideShapeSettings settings;
  settings.mMaxSeparationDistance = 0.05f;
  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
  w.system.GetNarrowPhaseQuery().CollideShape(c.character->GetShape(), JPH::Vec3::sReplicate(1.0f),
                                              c.character->GetCenterOfMassTransform(), settings, JPH::RVec3::sZero(),
                                              hits, w.system.GetDefaultBroadPhaseLayerFilter(layers::moving),
                                              w.system.GetDefaultLayerFilter(layers::moving));
  std::vector<u32> now;
  for (const JPH::CollideShapeResult &hit : hits.mHits) {
    const body3d_handle body = handle_by_id(w, hit.mBodyID2);
    if (body.id == 0 || std::find(now.begin(), now.end(), body.id) != now.end())
      continue;
    now.push_back(body.id);
    if (std::binary_search(c.touching.begin(), c.touching.end(), body.id))
      continue;
    const JPH::Vec3 axis = hit.mPenetrationAxis;
    const vec3 normal = axis.LengthSq() > 1e-12f ? nv(-axis.Normalized()) : vec3{0.0f, 1.0f, 0.0f};
    bool sensor = false;
    JPH::BodyLockRead lock(w.system.GetBodyLockInterface(), hit.mBodyID2);
    if (lock.Succeeded())
      sensor = lock.GetBody().IsSensor();
    w.contacts.push_back(contact3d{.a = body,
                                   .b = {},
                                   .character = {handle},
                                   .began = true,
                                   .sensor = sensor,
                                   .point = nv(hit.mContactPointOn2),
                                   .normal = normal});
  }
  std::sort(now.begin(), now.end());
  for (u32 old : c.touching)
    if (!std::binary_search(now.begin(), now.end(), old))
      w.contacts.push_back(contact3d{.a = {old}, .b = {}, .character = {handle}, .began = false, .sensor = false,
                                     .point = {}, .normal = {}});
  c.touching = std::move(now);
}

body_slot *body_of(const context &ctx, body3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->bodies.size())
    return nullptr;
  body_slot &b = w->bodies[h.id - 1];
  return b.alive ? &b : nullptr;
}

character_slot *character_of(const context &ctx, character3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->characters.size())
    return nullptr;
  character_slot &c = w->characters[h.id - 1];
  return c.alive ? &c : nullptr;
}

// The njin handle of a Jolt body (kept in the body's user data).
body3d_handle handle_of(physics3d_world &w, JPH::BodyID id) {
  if (id.IsInvalid())
    return body3d_handle{};
  return body3d_handle{(u32)w.system.GetBodyInterface().GetUserData(id)};
}
} // namespace

physics3d_state::physics3d_state() = default;

physics3d_state::~physics3d_state() {
  if (world) {
    world.reset();
    jolt_release();
  }
}

void physics3d_step(context &ctx, f32 dt) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || dt <= 0.0f)
    return;
  w->contacts.clear();
  JPH::BodyInterface &bi = w->system.GetBodyInterface();
  entt::registry &reg = world(ctx);
  // Kinematic components go where their entity is.
  for (auto [e, b, t] : reg.view<const body3d, const transform3d>().each()) {
    body_slot *slot = body_of(ctx, b.handle);
    if (slot != nullptr && slot->kinematic) {
      slot->has_target = true;
      slot->target_pos = t.position;
      slot->target_rot = t.rotation;
    }
  }
  for (body_slot &b : w->bodies) {
    if (b.alive && b.kinematic && b.has_target) {
      bi.MoveKinematic(b.id, jv(b.target_pos), quat_of(b.target_rot), dt);
      b.has_target = false;
    }
  }
  if (w->static_added >= 32) {
    w->system.OptimizeBroadPhase();
    w->static_added = 0;
  }
  const JPH::Vec3 gravity = jv(ctx.physics3d.gravity);
  w->crowd.rebuild(dt);
  for (character_slot &c : w->characters) {
    if (!c.alive || !c.active)
      continue;
    c.character->UpdateGroundVelocity();
    c.character->SetLinearVelocity(jv(c.desired));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, c.step_height, 0.0f);
    c.character->ExtendedUpdate(dt, gravity, settings, w->system.GetDefaultBroadPhaseLayerFilter(layers::moving),
                                w->system.GetDefaultLayerFilter(layers::moving), {}, {}, w->temp);
    // Its weight on what it stands on. For the solve, a dynamic body it stands
    // on carries it: as heavy as body and rider together, with the rider's
    // inertia where it stands and the turn its weight gives there about the
    // body's centre (gravity already pulls the added mass down). The solver
    // then holds a 70 kg person on a 3 kg board as the one heavy thing they
    // are: a board on the floor stays put, a seesaw tips, a board leant on a
    // wall slips out from under them. Its whole weight as a force on the
    // light board alone kicked it more each step than the contacts could
    // take back, and the board shook. Shared among every point it stands on:
    // one foot on each of two boards presses both.
    if (c.mass > 0.0f && c.character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround) {
      const JPH::CharacterVirtual *ch = c.character.GetPtr();
      i32 count = 0;
      for (const JPH::CharacterContact &k : ch->GetActiveContacts())
        if (k.mHadCollision && !k.mWasDiscarded && !k.mIsSensorB && !k.mBodyB.IsInvalid() &&
            character_listener::supports(ch, k))
          count++;
      if (count > 0) {
        const f32 share = c.mass / (f32)count;
        for (const JPH::CharacterContact &k : ch->GetActiveContacts()) {
          if (!k.mHadCollision || k.mWasDiscarded || k.mIsSensorB || k.mBodyB.IsInvalid() ||
              !character_listener::supports(ch, k) || k.mMotionTypeB != JPH::EMotionType::Dynamic)
            continue;
          {
            JPH::BodyLockWrite lock(w->system.GetBodyLockInterface(), k.mBodyB);
            if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
              continue;
            JPH::Body &body = lock.GetBody();
            JPH::MotionProperties *mp = body.GetMotionProperties();
            const JPH::Vec3 inv_inertia = mp->GetInverseInertiaDiagonal();
            if (mp->GetInverseMass() <= 0.0f || inv_inertia.ReduceMin() <= 0.0f)
              continue;
            // Where it stands, from the centre of mass in the body's axes, kept a
            // little inside the body: a contact on a board's very edge (the
            // capsule over a gap) can lie outside it.
            const JPH::Vec3 com = body.GetShape()->GetCenterOfMass();
            const JPH::AABox box = body.GetShape()->GetLocalBounds();
            const JPH::Vec3 inset = JPH::Vec3::sMin(box.GetExtent() * 0.5f, JPH::Vec3::sReplicate(0.01f));
            const JPH::Vec3 r =
                JPH::Vec3::sClamp(JPH::Vec3(body.GetInverseCenterOfMassTransform() * k.mPosition) + com,
                                  box.mMin + inset, box.mMax - inset) - com;
            bool seen = false;
            for (const physics3d_world::ridden &q : w->ridden_bodies)
              seen = seen || q.id == k.mBodyB;
            if (!seen)
              w->ridden_bodies.push_back({k.mBodyB, mp->GetInverseMass(), inv_inertia, mp->GetInertiaRotation()});
            // The rider as a point mass at r: m r.r E - m r r^T on the inertia.
            const f32 rr = r.Dot(r);
            const JPH::Mat44 point(JPH::Vec4(rr - r.GetX() * r.GetX(), -r.GetY() * r.GetX(), -r.GetZ() * r.GetX(), 0.0f),
                                   JPH::Vec4(-r.GetX() * r.GetY(), rr - r.GetY() * r.GetY(), -r.GetZ() * r.GetY(), 0.0f),
                                   JPH::Vec4(-r.GetX() * r.GetZ(), -r.GetY() * r.GetZ(), rr - r.GetZ() * r.GetZ(), 0.0f),
                                   JPH::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
            JPH::MassProperties carried;
            carried.mMass = 1.0f / mp->GetInverseMass() + share;
            carried.mInertia = mp->GetLocalSpaceInverseInertia().Inversed3x3() + point * share;
            mp->SetMassProperties(mp->GetAllowedDOFs(), carried);
            body.AddTorque((body.GetRotation() * r).Cross(gravity * share));
          }
          bi.ActivateBody(k.mBodyB);
        }
      }
    }
  }
  // Pushers parted where the step left them overlapping (character3d_desc::push).
  w->crowd.part(2);
  w->system.Update(dt, 1, &w->temp, &w->jobs);
  // Bodies ridden this step go back to their own mass and inertia.
  for (const physics3d_world::ridden &q : w->ridden_bodies) {
    JPH::BodyLockWrite lock(w->system.GetBodyLockInterface(), q.id);
    if (lock.Succeeded() && lock.GetBody().IsDynamic()) {
      JPH::MotionProperties *mp = lock.GetBody().GetMotionProperties();
      mp->SetInverseMass(q.inv_mass);
      mp->SetInverseInertia(q.inv_inertia, q.inertia_rotation);
    }
  }
  w->ridden_bodies.clear();
  for (usize i = 0; i < w->characters.size(); i++)
    if (w->characters[i].alive && w->characters[i].active)
      character_contacts(*w, w->characters[i], (u32)(i + 1));

  // Dynamic bodies and characters move their entities.
  for (auto [e, b, t] : reg.view<const body3d, transform3d>().each()) {
    body_slot *slot = body_of(ctx, b.handle);
    if (slot == nullptr || !slot->dynamic)
      continue;
    const transform3d now = body3d_transform(ctx, b.handle);
    t.position = now.position;
    t.rotation = now.rotation;
  }
  for (auto [e, c, t] : reg.view<const character3d, transform3d>().each())
    if (character_of(ctx, c.handle) != nullptr)
      t.position = character3d_position(ctx, c.handle);
}

body3d_handle body3d_create(context &ctx, const body3d_desc &desc) {
  // The world first: it sets up Jolt's allocator, which shapes need.
  physics3d_world &w = world_of(ctx);
  JPH::RefConst<JPH::Shape> shape = desc.model.id != 0 ? make_model_shape(ctx, desc) : make_shape(desc);
  if (shape == nullptr)
    return body3d_handle{};
  const JPH::EMotionType motion = desc.motion == body3d_dynamic     ? JPH::EMotionType::Dynamic
                                  : desc.motion == body3d_kinematic ? JPH::EMotionType::Kinematic
                                                                    : JPH::EMotionType::Static;
  JPH::BodyCreationSettings settings(shape, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
                                     quat_of(desc.rotation), motion,
                                     desc.motion == body3d_static ? layers::still : layers::moving);
  settings.mFriction = desc.friction;
  settings.mRestitution = desc.restitution;
  settings.mIsSensor = desc.sensor;
  // A sensor that does not move still notices kinematic bodies passing.
  settings.mCollideKinematicVsNonDynamic = desc.sensor;
  if (desc.motion == body3d_dynamic) {
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.001f);
  } else if (desc.motion == body3d_kinematic && desc.model.id != 0) {
    // A triangle mesh has no volume to compute mass from; a kinematic body
    // never uses it anyway.
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.mMass = 1.0f;
    settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sIdentity();
  }
  const u32 handle = (u32)w.bodies.size() + 1;
  settings.mUserData = handle;
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  const JPH::BodyID id = bi.CreateAndAddBody(
      settings, desc.motion == body3d_static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
  if (desc.motion == body3d_static)
    w.static_added++;
  if (id.IsInvalid()) {
    NJIN_WARN("physics3d: body limit reached");
    return body3d_handle{};
  }
  w.handle_by_body[id.GetIndexAndSequenceNumber()] = handle;
  w.bodies.push_back(body_slot{.id = id,
                               .alive = true,
                               .kinematic = desc.motion == body3d_kinematic,
                               .dynamic = desc.motion == body3d_dynamic,
                               .user = desc.user,
                               .has_target = false,
                               .target_pos = desc.position,
                               .target_rot = desc.rotation});
  return body3d_handle{handle};
}

namespace {
// Everything that refers to body `handle` besides its Jolt body: joints,
// contact pairs, the id map.
void forget_body(context &ctx, physics3d_world &w, body3d_handle handle) {
  // Its joints first: a constraint must not outlive a body it holds.
  for (usize i = 0; i < w.joints.size(); i++)
    if (w.joints[i].alive && (w.joints[i].a.id == handle.id || w.joints[i].b.id == handle.id))
      joint3d_destroy(ctx, joint3d_handle{(u32)(i + 1)});
  // A destroyed body stops touching without an event.
  for (auto it = w.touching.begin(); it != w.touching.end();)
    it = it->second.a.id == handle.id || it->second.b.id == handle.id ? w.touching.erase(it) : std::next(it);
  for (character_slot &c : w.characters)
    std::erase(c.touching, handle.id);
  w.handle_by_body.erase(w.bodies[handle.id - 1].id.GetIndexAndSequenceNumber());
}
} // namespace

void body3d_destroy(context &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return;
  if (b->ragdoll) {
    NJIN_WARN("physics3d: body3d_destroy on a part of a ragdoll: use ragdoll3d_destroy");
    return;
  }
  physics3d_world &w = *ctx.physics3d.world;
  forget_body(ctx, w, handle);
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  bi.RemoveBody(b->id);
  bi.DestroyBody(b->id);
  *b = body_slot{};
}

transform3d body3d_transform(const context &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return transform3d{};
  const JPH::BodyInterface &bi = ctx.physics3d.world->system.GetBodyInterface();
  JPH::RVec3 p;
  JPH::Quat q;
  bi.GetPositionAndRotation(b->id, p, q);
  return transform3d{.position = {(f32)p.GetX(), (f32)p.GetY(), (f32)p.GetZ()}, .rotation = degrees_of(q)};
}

void body3d_set_position(context &ctx, body3d_handle handle, vec3 position, vec3 rotation) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return;
  ctx.physics3d.world->system.GetBodyInterface().SetPositionAndRotation(
      b->id, JPH::RVec3(position.x, position.y, position.z), quat_of(rotation), JPH::EActivation::Activate);
  b->has_target = false;
}

void body3d_move_kinematic(context &ctx, body3d_handle handle, vec3 position, vec3 rotation) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return;
  if (!b->kinematic) {
    NJIN_WARN("physics3d: body3d_move_kinematic on a body that is not kinematic");
    return;
  }
  b->has_target = true;
  b->target_pos = position;
  b->target_rot = rotation;
}

vec3 body3d_velocity(const context &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return vec3{};
  return nv(ctx.physics3d.world->system.GetBodyInterface().GetLinearVelocity(b->id));
}

void body3d_set_velocity(context &ctx, body3d_handle handle, vec3 velocity) {
  body_slot *b = body_of(ctx, handle);
  if (b != nullptr)
    ctx.physics3d.world->system.GetBodyInterface().SetLinearVelocity(b->id, jv(velocity));
}

void body3d_add_impulse(context &ctx, body3d_handle handle, vec3 impulse) {
  body_slot *b = body_of(ctx, handle);
  if (b != nullptr)
    ctx.physics3d.world->system.GetBodyInterface().AddImpulse(b->id, jv(impulse));
}

u64 body3d_user(const context &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  return b != nullptr ? b->user : 0;
}

character3d_handle character3d_create(context &ctx, const character3d_desc &desc) {
  physics3d_world &w = world_of(ctx);
  const f32 r = std::max(desc.radius, 0.01f);
  const f32 half_height = std::max(desc.height * 0.5f, r + 0.01f);
  // The capsule stands on the origin: the character's position is its feet.
  JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
  settings->mShape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, half_height, 0.0f), JPH::Quat::sIdentity(),
                                                         new JPH::CapsuleShape(half_height - r, r))
                         .Create()
                         .Get();
  settings->mMaxSlopeAngle = JPH::DegreesToRadians(desc.max_slope);
  // Jolt would put the weight on the ground body as an impulse straight into its
  // velocity, outside the solver: a light board lying on the floor then jumps
  // on every step. The step below puts it on as a force instead (mass 0 here
  // turns Jolt's off; pushing bodies aside goes by mMaxStrength, not mass).
  settings->mMass = 0.0f;
  // Supported only by the lower sphere of the capsule, not its side.
  settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -r);
  character_slot slot;
  slot.character = new JPH::CharacterVirtual(
      settings, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z), JPH::Quat::sIdentity(), &w.system);
  slot.alive = true;
  slot.step_height = desc.step_height;
  slot.mass = std::max(desc.mass, 0.0f);
  slot.character->SetCharacterVsCharacterCollision(&w.crowd);
  slot.character->SetListener(&w.characters_listener);
  w.crowd.add(slot.character.GetPtr());
  if (desc.push)
    w.crowd.pushers[slot.character.GetPtr()] = {r, std::max(desc.mass, 1.0f)};
  w.characters.push_back(std::move(slot));
  return character3d_handle{(u32)w.characters.size()};
}

void character3d_destroy(context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c != nullptr) {
    ctx.physics3d.world->crowd.forget(c->character.GetPtr());
    *c = character_slot{};
  }
}

void character3d_set_active(context &ctx, character3d_handle handle, bool active) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr || c->active == active)
    return;
  c->active = active;
  ctx.physics3d.world->crowd.set_active(c->character.GetPtr(), active);
  c->touching.clear();
}

bool character3d_active(const context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  return c != nullptr && c->active;
}

void character3d_set_velocity(context &ctx, character3d_handle handle, vec3 velocity) {
  if (character_slot *c = character_of(ctx, handle))
    c->desired = velocity;
}

vec3 character3d_velocity(const context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  return c != nullptr ? nv(c->character->GetLinearVelocity()) : vec3{};
}

vec3 character3d_position(const context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr)
    return vec3{};
  const JPH::RVec3 p = c->character->GetPosition();
  return vec3{(f32)p.GetX(), (f32)p.GetY(), (f32)p.GetZ()};
}

void character3d_set_position(context &ctx, character3d_handle handle, vec3 position) {
  if (character_slot *c = character_of(ctx, handle)) {
    c->character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
    c->character->SetLinearVelocity(JPH::Vec3::sZero());
    c->desired = vec3{};
  }
}

bool character3d_grounded(const context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  return c != nullptr && c->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

vec3 character3d_ground_velocity(const context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr || c->character->GetGroundState() == JPH::CharacterBase::EGroundState::InAir)
    return vec3{};
  return nv(c->character->GetGroundVelocity());
}

body3d_handle character3d_ground_body(const context &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr || c->character->GetGroundState() == JPH::CharacterBase::EGroundState::InAir)
    return body3d_handle{};
  return handle_of(*ctx.physics3d.world, c->character->GetGroundBodyID());
}

namespace {
// Rays go through sensors: a pickup must not stop a bullet or the camera.
class not_sensor final : public JPH::BodyFilter {
public:
  bool ShouldCollideLocked(const JPH::Body &body) const override { return !body.IsSensor(); }
};
} // namespace

namespace {
// Everything but sensors and one body.
class not_sensor_or final : public JPH::BodyFilter {
public:
  JPH::BodyID skip;
  bool ShouldCollideLocked(const JPH::Body &body) const override { return !body.IsSensor() && body.GetID() != skip; }
};

// The minimum translation vector taking `shape` (at `centre`, turned by `turn`)
// out of the bodies it overlaps: out of the deepest overlap, then look again
// from there, as a corner takes two.
vec3 push_out(const context &ctx, const JPH::Shape *shape, JPH::QuatArg turn, vec3 centre, body3d_handle ignore) {
  physics3d_world *w = ctx.physics3d.world.get();
  not_sensor_or filter;
  if (const body_slot *s = body_of(ctx, ignore))
    filter.skip = s->id;
  vec3 push{};
  for (int k = 0; k < 4; k++) {
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    const vec3 mid = centre + push;
    const JPH::RMat44 at = JPH::RMat44::sRotationTranslation(turn, JPH::RVec3(mid.x, mid.y, mid.z));
    w->system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), at, JPH::CollideShapeSettings(),
                                                 JPH::RVec3::sZero(), hits,
                                                 w->system.GetDefaultBroadPhaseLayerFilter(layers::moving),
                                                 w->system.GetDefaultLayerFilter(layers::moving), filter);
    const JPH::CollideShapeResult *deepest = nullptr;
    for (const JPH::CollideShapeResult &hit : hits.mHits)
      if (hit.mPenetrationDepth > 1e-4f && (deepest == nullptr || hit.mPenetrationDepth > deepest->mPenetrationDepth))
        deepest = &hit;
    if (deepest == nullptr || deepest->mPenetrationAxis.LengthSq() < 1e-12f)
      break;
    push = push + nv(-deepest->mPenetrationAxis.Normalized()) * (deepest->mPenetrationDepth + 1e-3f);
  }
  return push;
}

} // namespace

vec3 physics3d_capsule_push(const context &ctx, vec3 a, vec3 b, f32 radius, body3d_handle ignore) {
  if (ctx.physics3d.world == nullptr || radius <= 0.0f)
    return {};
  const vec3 axis = b - a;
  const f32 len = length(axis);
  JPH::ShapeSettings::ShapeResult made = len > 1e-4f ? JPH::CapsuleShapeSettings(len * 0.5f, radius).Create()
                                                     : JPH::SphereShapeSettings(radius).Create();
  if (made.HasError())
    return {};
  const JPH::Quat turn = len > 1e-4f ? JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), jv(axis / len)) : JPH::Quat::sIdentity();
  return push_out(ctx, made.Get().GetPtr(), turn, (a + b) * 0.5f, ignore);
}

vec3 physics3d_box_push(const context &ctx, vec3 center, vec3 rotation, vec3 size, body3d_handle ignore) {
  if (ctx.physics3d.world == nullptr)
    return {};
  const JPH::Vec3 half = JPH::Vec3::sMax(jv(size * 0.5f), JPH::Vec3::sReplicate(0.005f));
  JPH::ShapeSettings::ShapeResult made =
      JPH::BoxShapeSettings(half, std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f)).Create();
  if (made.HasError())
    return {};
  return push_out(ctx, made.Get().GetPtr(), quat_of(rotation), center, ignore);
}

namespace {
// The first thing `shape` (at `centre`, turned by `turn`) meets moving along
// `motion`; what it already overlaps at the start is not in its way.
ray3d_hit cast_out(const context &ctx, const JPH::Shape *shape, JPH::QuatArg turn, vec3 centre, vec3 motion,
                   body3d_handle ignore, body3d_handle *body) {
  if (body != nullptr)
    *body = body3d_handle{};
  physics3d_world *w = ctx.physics3d.world.get();
  const f32 reach = length(motion);
  if (w == nullptr || reach < 1e-6f)
    return ray3d_hit{};
  const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
      shape, JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(turn, JPH::RVec3(centre.x, centre.y, centre.z)),
      jv(motion));
  JPH::ShapeCastSettings settings;
  settings.mReturnDeepestPoint = false;
  not_sensor_or filter;
  if (const body_slot *s = body_of(ctx, ignore))
    filter.skip = s->id;
  JPH::AllHitCollisionCollector<JPH::CastShapeCollector> hits;
  w->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), hits,
                                            w->system.GetDefaultBroadPhaseLayerFilter(layers::moving),
                                            w->system.GetDefaultLayerFilter(layers::moving), filter);
  const JPH::ShapeCastResult *first = nullptr;
  for (const JPH::ShapeCastResult &hit : hits.mHits)
    if (hit.mFraction > 1e-5f && (first == nullptr || hit.mFraction < first->mFraction))
      first = &hit;
  if (first == nullptr)
    return ray3d_hit{};
  if (body != nullptr)
    *body = handle_by_id(*w, first->mBodyID2);
  const JPH::Vec3 axis = first->mPenetrationAxis;
  return ray3d_hit{.hit = true,
                   .distance = first->mFraction * reach,
                   .point = world_vec(first->mContactPointOn2),
                   .normal = axis.LengthSq() > 1e-12f ? nv(-axis.Normalized()) : normalize(motion * -1.0f)};
}

const JPH::Shape *hull_of(const context &ctx, hull3d_handle h) {
  const physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->hulls.size())
    return nullptr;
  return w->hulls[h.id - 1].GetPtr();
}
} // namespace

ray3d_hit physics3d_box_cast(const context &ctx, vec3 center, vec3 rotation, vec3 size, vec3 motion,
                             body3d_handle ignore, body3d_handle *body) {
  if (body != nullptr)
    *body = body3d_handle{};
  if (ctx.physics3d.world == nullptr || length(motion) < 1e-6f)
    return ray3d_hit{};
  const JPH::Vec3 half = JPH::Vec3::sMax(jv(size * 0.5f), JPH::Vec3::sReplicate(0.005f));
  JPH::ShapeSettings::ShapeResult made =
      JPH::BoxShapeSettings(half, std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f)).Create();
  if (made.HasError())
    return ray3d_hit{};
  return cast_out(ctx, made.Get().GetPtr(), quat_of(rotation), center, motion, ignore, body);
}

hull3d_handle physics3d_hull_create(context &ctx, const vec3 *points, i32 count) {
  physics3d_world &w = world_of(ctx);
  if (points == nullptr || count < 4) {
    NJIN_WARN("physics3d: physics3d_hull_create needs at least 4 points");
    return hull3d_handle{};
  }
  JPH::Array<JPH::Vec3> pts;
  pts.reserve((usize)count);
  for (i32 i = 0; i < count; i++)
    pts.push_back(jv(points[i]));
  // A thin rounding keeps casts and pushes steady on sharp corners.
  JPH::ShapeSettings::ShapeResult made = JPH::ConvexHullShapeSettings(pts, 0.005f).Create();
  if (made.HasError()) {
    NJIN_WARN("physics3d: physics3d_hull_create: %s", made.GetError().c_str());
    return hull3d_handle{};
  }
  w.hulls.push_back(made.Get());
  return hull3d_handle{(u32)w.hulls.size()};
}

void physics3d_hull_destroy(context &ctx, hull3d_handle hull) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w != nullptr && hull.id > 0 && hull.id <= w->hulls.size())
    w->hulls[hull.id - 1] = nullptr;
}

vec3 physics3d_hull_push(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, body3d_handle ignore) {
  const JPH::Shape *shape = hull_of(ctx, hull);
  if (shape == nullptr)
    return {};
  // Jolt keeps a shape about its centre of mass: place that, not the origin.
  const JPH::Quat turn = quat_of(rotation);
  return push_out(ctx, shape, turn, position + nv(turn * shape->GetCenterOfMass()), ignore);
}

ray3d_hit physics3d_hull_cast(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, vec3 motion,
                              body3d_handle ignore, body3d_handle *body) {
  const JPH::Shape *shape = hull_of(ctx, hull);
  if (shape == nullptr) {
    if (body != nullptr)
      *body = body3d_handle{};
    return ray3d_hit{};
  }
  // A cast takes the shape's own transform (it adds the centre of mass itself).
  return cast_out(ctx, shape, quat_of(rotation), position, motion, ignore, body);
}

i32 physics3d_hull_lines(const context &ctx, hull3d_handle hull, vec3 *out, i32 count) {
  const auto *shape = static_cast<const JPH::ConvexHullShape *>(hull_of(ctx, hull));
  if (shape == nullptr)
    return 0;
  const JPH::Vec3 com = shape->GetCenterOfMass();
  i32 n = 0;
  JPH::uint idx[256];
  for (JPH::uint f = 0; f < shape->GetNumFaces(); f++) {
    const JPH::uint k = std::min<JPH::uint>(shape->GetNumVerticesInFace(f), 256u);
    shape->GetFaceVertices(f, k, idx);
    for (JPH::uint e = 0; e < k; e++) {
      for (JPH::uint end : {idx[e], idx[(e + 1) % k]}) {
        if (out != nullptr && n < count)
          out[n] = nv(shape->GetPoint(end) + com);
        n++;
      }
    }
  }
  return n;
}

ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance, body3d_handle *body) {
  if (body != nullptr)
    *body = body3d_handle{};
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || max_distance <= 0.0f)
    return ray3d_hit{};
  const vec3 dir = normalize(ray.direction);
  const JPH::RRayCast cast{JPH::RVec3(ray.origin.x, ray.origin.y, ray.origin.z), jv(dir * max_distance)};
  JPH::RayCastResult result;
  const not_sensor bodies;
  if (!w->system.GetNarrowPhaseQuery().CastRay(cast, result, {}, {}, bodies))
    return ray3d_hit{};
  const JPH::RVec3 p = cast.GetPointOnRay(result.mFraction);
  vec3 normal{};
  JPH::BodyLockRead lock(w->system.GetBodyLockInterface(), result.mBodyID);
  if (lock.Succeeded())
    normal = nv(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, p));
  if (body != nullptr)
    *body = handle_of(*w, result.mBodyID);
  return ray3d_hit{.hit = true,
                   .distance = result.mFraction * max_distance,
                   .point = {(f32)p.GetX(), (f32)p.GetY(), (f32)p.GetZ()},
                   .normal = normal};
}

i32 physics3d_contact_count(const context &ctx) {
  const physics3d_world *w = ctx.physics3d.world.get();
  return w != nullptr ? (i32)w->contacts.size() : 0;
}

contact3d physics3d_contact(const context &ctx, i32 index) {
  const physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || index < 0 || index >= (i32)w->contacts.size())
    return contact3d{};
  return w->contacts[(usize)index];
}

namespace {
joint_slot *joint_of(const context &ctx, joint3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->joints.size())
    return nullptr;
  joint_slot &j = w->joints[h.id - 1];
  return j.alive ? &j : nullptr;
}

JPH::RVec3 rv(vec3 v) { return JPH::RVec3(v.x, v.y, v.z); }

// Limits of a hinge (radians) or slider: Jolt needs min <= 0 <= max.
bool limits(const joint3d_desc &d, f32 scale, f32 bound, f32 &lo, f32 &hi) {
  if (d.min >= d.max)
    return false;
  lo = clamp(d.min * scale, -bound, 0.0f);
  hi = clamp(d.max * scale, 0.0f, bound);
  return lo < hi;
}
} // namespace

joint3d_handle joint3d_create(context &ctx, const joint3d_desc &desc) {
  body_slot *a = body_of(ctx, desc.a);
  if (a == nullptr) {
    NJIN_WARN("physics3d: joint3d_create needs a valid body a");
    return joint3d_handle{};
  }
  body_slot *b = body_of(ctx, desc.b);
  physics3d_world &w = *ctx.physics3d.world;
  const JPH::Vec3 axis = jv(length_sq(desc.axis) > 1e-12f ? normalize(desc.axis) : vec3{0.0f, 1.0f, 0.0f});
  JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
  switch (desc.kind) {
  case joint3d_fixed: {
    auto *f = new JPH::FixedConstraintSettings();
    f->mAutoDetectPoint = true;
    settings = f;
    break;
  }
  case joint3d_point: {
    auto *p = new JPH::PointConstraintSettings();
    p->mPoint1 = p->mPoint2 = rv(desc.anchor);
    settings = p;
    break;
  }
  case joint3d_slider: {
    auto *sl = new JPH::SliderConstraintSettings();
    sl->mAutoDetectPoint = true;
    sl->SetSliderAxis(axis);
    f32 lo = 0.0f, hi = 0.0f;
    if (limits(desc, 1.0f, 1e6f, lo, hi)) {
      sl->mLimitsMin = lo;
      sl->mLimitsMax = hi;
    }
    if (desc.motor_force > 0.0f)
      sl->mMotorSettings.SetForceLimit(desc.motor_force);
    settings = sl;
    break;
  }
  case joint3d_distance: {
    auto *d = new JPH::DistanceConstraintSettings();
    d->mPoint1 = rv(desc.anchor);
    d->mPoint2 = rv(desc.anchor_b);
    if (desc.min > 0.0f || desc.max > 0.0f) {
      d->mMinDistance = std::max(desc.min, 0.0f);
      d->mMaxDistance = std::max(desc.max, d->mMinDistance);
    }
    settings = d;
    break;
  }
  case joint3d_hinge:
  default: {
    auto *h = new JPH::HingeConstraintSettings();
    h->mPoint1 = h->mPoint2 = rv(desc.anchor);
    h->mHingeAxis1 = h->mHingeAxis2 = axis;
    h->mNormalAxis1 = h->mNormalAxis2 = axis.GetNormalizedPerpendicular();
    f32 lo = 0.0f, hi = 0.0f;
    if (limits(desc, pi / 180.0f, pi, lo, hi)) {
      h->mLimitsMin = lo;
      h->mLimitsMax = hi;
    }
    if (desc.motor_force > 0.0f)
      h->mMotorSettings.SetTorqueLimit(desc.motor_force);
    settings = h;
    break;
  }
  }
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  JPH::TwoBodyConstraint *constraint = bi.CreateConstraint(settings, a->id, b != nullptr ? b->id : JPH::BodyID());
  if (constraint == nullptr) {
    NJIN_WARN("physics3d: joint could not be created");
    return joint3d_handle{};
  }
  w.system.AddConstraint(constraint);
  bi.ActivateConstraint(constraint);
  if (desc.motor_force > 0.0f) {
    if (desc.kind == joint3d_hinge)
      static_cast<JPH::HingeConstraint *>(constraint)->SetMotorState(JPH::EMotorState::Velocity);
    else if (desc.kind == joint3d_slider)
      static_cast<JPH::SliderConstraint *>(constraint)->SetMotorState(JPH::EMotorState::Velocity);
  }
  w.joints.push_back(joint_slot{.constraint = constraint,
                                .alive = true,
                                .kind = desc.kind,
                                .a = desc.a,
                                .b = b != nullptr ? desc.b : body3d_handle{}});
  return joint3d_handle{(u32)w.joints.size()};
}

void joint3d_destroy(context &ctx, joint3d_handle handle) {
  joint_slot *j = joint_of(ctx, handle);
  if (j == nullptr)
    return;
  physics3d_world &w = *ctx.physics3d.world;
  w.system.GetBodyInterface().ActivateConstraint(j->constraint);
  w.system.RemoveConstraint(j->constraint);
  *j = joint_slot{};
}

void joint3d_set_motor(context &ctx, joint3d_handle handle, f32 speed) {
  joint_slot *j = joint_of(ctx, handle);
  if (j == nullptr)
    return;
  if (j->kind == joint3d_hinge) {
    auto *h = static_cast<JPH::HingeConstraint *>(j->constraint.GetPtr());
    if (h->GetMotorState() == JPH::EMotorState::Off)
      return;
    h->SetTargetAngularVelocity(speed * (pi / 180.0f));
  } else if (j->kind == joint3d_slider) {
    auto *sl = static_cast<JPH::SliderConstraint *>(j->constraint.GetPtr());
    if (sl->GetMotorState() == JPH::EMotorState::Off)
      return;
    sl->SetTargetVelocity(speed);
  } else {
    return;
  }
  ctx.physics3d.world->system.GetBodyInterface().ActivateConstraint(j->constraint);
}

f32 joint3d_position(const context &ctx, joint3d_handle handle) {
  const joint_slot *j = joint_of(ctx, handle);
  if (j == nullptr)
    return 0.0f;
  if (j->kind == joint3d_hinge)
    return static_cast<const JPH::HingeConstraint *>(j->constraint.GetPtr())->GetCurrentAngle() * (180.0f / pi);
  if (j->kind == joint3d_slider)
    return static_cast<const JPH::SliderConstraint *>(j->constraint.GetPtr())->GetCurrentPosition();
  return 0.0f;
}

namespace {
ragdoll_slot *ragdoll_of(const context &ctx, ragdoll3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->ragdolls.size())
    return nullptr;
  ragdoll_slot &r = w->ragdolls[h.id - 1];
  return r.alive ? &r : nullptr;
}

// A bone's frame as model_bone_pose() gives it (axes as columns).
JPH::Mat44 bone_mat(const bone_pose3d &p) {
  return JPH::Mat44(JPH::Vec4(jv(p.x_axis), 0.0f), JPH::Vec4(jv(p.y_axis), 0.0f), JPH::Vec4(jv(p.z_axis), 0.0f),
                    JPH::Vec4(jv(p.position), 1.0f));
}

// Where draw_model_anim() puts the model: scale, then rotation, then position.
JPH::Mat44 draw_mat(const transform3d &t) {
  return JPH::Mat44::sRotationTranslation(quat_of(t.rotation), jv(t.position)) * JPH::Mat44::sScale(jv(t.scale));
}

// A frame with its scale taken out: rotation and position only.
JPH::Mat44 unscaled(JPH::Mat44Arg m) {
  const JPH::Vec3 x = m.GetAxisX().NormalizedOr(JPH::Vec3::sAxisX());
  const JPH::Vec3 z = x.Cross(m.GetAxisY()).NormalizedOr(JPH::Vec3::sAxisZ());
  const JPH::Vec3 y = z.Cross(x);
  return JPH::Mat44(JPH::Vec4(x, 0.0f), JPH::Vec4(y, 0.0f), JPH::Vec4(z, 0.0f), JPH::Vec4(m.GetTranslation(), 1.0f));
}
} // namespace

ragdoll3d_handle ragdoll3d_create(context &ctx, const ragdoll3d_desc &desc) {
  physics3d_world &w = world_of(ctx);
  const model_slot *m = model_slot_of(ctx.model, desc.model);
  if (m == nullptr || m->model.skeleton.boneCount == 0) {
    NJIN_WARN("physics3d: ragdoll3d_create needs a loaded model with bones");
    return ragdoll3d_handle{};
  }
  const i32 n = (i32)desc.bone_count;
  if (desc.bones == nullptr || n == 0) {
    NJIN_WARN("physics3d: ragdoll3d_create needs at least one bone");
    return ragdoll3d_handle{};
  }
  const i32 bones = m->model.skeleton.boneCount;
  const BoneInfo *info = m->model.skeleton.bones;
  std::vector<i32> bone_of((usize)n), part_at((usize)bones, -1);
  for (i32 i = 0; i < n; i++) {
    const char *name = desc.bones[i].name != nullptr ? desc.bones[i].name : "";
    const i32 b = model_bone_find(ctx, desc.model, name);
    if (b < 0 || part_at[(usize)b] >= 0) {
      NJIN_WARN("physics3d: ragdoll3d_create: bone '%s' %s", name, b < 0 ? "not found" : "listed twice");
      return ragdoll3d_handle{};
    }
    bone_of[(usize)i] = b;
    part_at[(usize)b] = i;
  }
  // The entry a bone hangs from: the nearest listed bone at or above it.
  const auto part_above = [&](i32 b) {
    for (i32 guard = 0; b >= 0 && guard < bones; guard++, b = info[b].parent)
      if (part_at[(usize)b] >= 0)
        return part_at[(usize)b];
    return -1;
  };
  std::vector<i32> parent((usize)n), depth((usize)n, 0);
  i32 root = -1;
  for (i32 i = 0; i < n; i++) {
    const i32 up = info[bone_of[(usize)i]].parent;
    parent[(usize)i] = up >= 0 ? part_above(up) : -1;
    if (parent[(usize)i] >= 0)
      continue;
    if (root >= 0) {
      NJIN_WARN("physics3d: ragdoll3d_create: '%s' and '%s' both have no listed bone above them",
                info[bone_of[(usize)root]].name, info[bone_of[(usize)i]].name);
      return ragdoll3d_handle{};
    }
    root = i;
  }
  for (i32 i = 0; i < n; i++)
    for (i32 p = parent[(usize)i]; p >= 0; p = parent[(usize)p])
      depth[(usize)i]++;
  // Jolt wants parents before children.
  std::vector<i32> order((usize)n);
  for (i32 i = 0; i < n; i++)
    order[(usize)i] = i;
  std::stable_sort(order.begin(), order.end(), [&](i32 a, i32 b) { return depth[(usize)a] < depth[(usize)b]; });
  std::vector<i32> joint_of_part((usize)n);
  for (i32 j = 0; j < n; j++)
    joint_of_part[(usize)order[(usize)j]] = j;

  // Bone frames in model space: the file's rest pose (the zero of every
  // limit) and the starting pose.
  std::vector<JPH::Mat44> rest((usize)bones), start((usize)bones);
  for (i32 b = 0; b < bones; b++) {
    rest[(usize)b] = bone_mat(model_bone_pose(ctx, desc.model, model_pose{}, b));
    start[(usize)b] = bone_mat(model_bone_pose(ctx, desc.model, desc.pose, b));
  }
  const JPH::Mat44 at = draw_mat(desc.transform);
  const f32 scale = std::max(std::abs(desc.transform.scale.y), 1e-6f);

  // The skin in the rest pose, each vertex with the part of the bone that
  // moves it most: a part's capsule wraps its bone and the unlisted ones below.
  struct skin_vertex {
    JPH::Vec3 p;
    i32 part;
  };
  std::vector<skin_vertex> skin;
  for (i32 k = 0; k < m->model.meshCount; k++) {
    const Mesh &mesh = m->model.meshes[k];
    if (mesh.vertices == nullptr || mesh.boneIndices == nullptr || mesh.boneWeights == nullptr)
      continue;
    for (i32 v = 0; v < mesh.vertexCount; v++) {
      i32 best = 0;
      for (i32 w = 1; w < 4; w++)
        if (mesh.boneWeights[v * 4 + w] > mesh.boneWeights[v * 4 + best])
          best = w;
      const i32 bone = mesh.boneIndices[v * 4 + best];
      const i32 part = bone < bones ? part_above(bone) : -1;
      if (part < 0)
        continue;
      const Vector3 q = Vector3Transform({mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]},
                                         m->model.transform);
      skin.push_back({at * JPH::Vec3(q.x, q.y, q.z), part});
    }
  }
  const auto percentile = [](std::vector<f32> &v, f32 k) {
    const usize i = std::min(v.size() - 1, (usize)((f32)(v.size() - 1) * k));
    std::nth_element(v.begin(), v.begin() + (isize)i, v.end());
    return v[i];
  };

  JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
  settings->mSkeleton = new JPH::Skeleton();
  settings->mParts.resize((usize)n);
  std::vector<f32> volume((usize)n);
  std::vector<shape3d> shapes((usize)n);
  f32 total = 0.0f;
  for (i32 j = 0; j < n; j++) {
    const i32 i = order[(usize)j];
    const ragdoll3d_bone &d = desc.bones[i];
    const i32 b = bone_of[(usize)i];
    settings->mSkeleton->AddJoint(info[b].name, parent[(usize)i] >= 0 ? joint_of_part[(usize)parent[(usize)i]] : -1);
    const JPH::Mat44 frame = unscaled(at * rest[(usize)b]);
    // The part's skin in the bone's frame: y along the bone.
    const JPH::Mat44 to_bone = frame.InversedRotationTranslation();
    std::vector<JPH::Vec3> local;
    for (const skin_vertex &v : skin)
      if (v.part == i)
        local.push_back(to_bone * v.p);
    const bool skinned = local.size() >= 8;
    // The capsule spans [t0, t1] along y (caps included), its axis at (cx, cz).
    f32 t0 = 0.0f, t1 = d.length, cx = 0.0f, cz = 0.0f;
    if (d.length <= 0.0f && skinned) {
      std::vector<f32> along;
      for (const JPH::Vec3 &l : local) {
        along.push_back(l.GetY());
        cx += l.GetX();
        cz += l.GetZ();
      }
      cx /= (f32)local.size();
      cz /= (f32)local.size();
      t0 = percentile(along, 0.02f);
      t1 = percentile(along, 0.98f);
    } else if (d.length <= 0.0f) {
      // No skin: to the child bone furthest along this one.
      const JPH::Vec3 dir = frame.GetAxisY();
      for (i32 c = 0; c < bones; c++)
        if (info[c].parent == b)
          t1 = std::max(t1, dir.Dot((at * rest[(usize)c]).GetTranslation() - frame.GetTranslation()));
    }
    f32 r = d.radius;
    if (r <= 0.0f && skinned) {
      std::vector<f32> radial;
      for (const JPH::Vec3 &l : local)
        if (l.GetY() >= t0 && l.GetY() <= t1)
          radial.push_back(std::hypot(l.GetX() - cx, l.GetZ() - cz));
      r = radial.empty() ? 0.06f : percentile(radial, 0.8f);
    } else if (r <= 0.0f) {
      r = 0.06f;
    }
    r = std::max(r, 0.01f);
    const f32 span = std::max(t1 - t0, 0.0f);
    const bool capsule = span > 2.0f * r + 0.01f;
    if (!capsule)
      r = std::max(r, span * 0.5f);
    shapes[(usize)i] = shape3d{.kind = capsule ? shape3d_capsule : shape3d_sphere,
                               .position = {cx, (t0 + t1) * 0.5f, cz},
                               .radius = r,
                               .height = capsule ? span : 2.0f * r};
    JPH::ShapeSettings::ShapeResult inner =
        capsule ? JPH::CapsuleShapeSettings(span * 0.5f - r, r).Create() : JPH::SphereShapeSettings(r).Create();
    if (inner.HasError()) {
      NJIN_WARN("physics3d: ragdoll3d_create: shape for '%s': %s", info[b].name, inner.GetError().c_str());
      return ragdoll3d_handle{};
    }
    // The body sits on the bone: its origin is the bone's, the shape runs up y.
    JPH::ShapeSettings::ShapeResult shape =
        JPH::RotatedTranslatedShapeSettings(JPH::Vec3(cx, (t0 + t1) * 0.5f, cz), JPH::Quat::sIdentity(), inner.Get())
            .Create();
    if (shape.HasError()) {
      NJIN_WARN("physics3d: ragdoll3d_create: shape for '%s': %s", info[b].name, shape.GetError().c_str());
      return ragdoll3d_handle{};
    }
    volume[(usize)j] = (capsule ? pi * r * r * (span - 2.0f * r) : 0.0f) + 4.0f / 3.0f * pi * r * r * r;
    total += volume[(usize)j];
    JPH::RagdollSettings::Part &part = settings->mParts[(usize)j];
    part.SetShape(shape.Get());
    part.mPosition = JPH::RVec3(frame.GetTranslation());
    part.mRotation = frame.GetQuaternion();
    part.mMotionType = JPH::EMotionType::Dynamic;
    part.mObjectLayer = layers::moving;
    part.mMotionQuality = JPH::EMotionQuality::LinearCast; // thin limbs, fast falls
    part.mFriction = desc.friction;
    if (parent[(usize)i] < 0)
      continue;
    // The joint at this bone's origin, in the rest pose.
    const JPH::RVec3 anchor(frame.GetTranslation());
    if (d.bend_min < d.bend_max) {
      auto *h = new JPH::HingeConstraintSettings();
      h->mPoint1 = h->mPoint2 = anchor;
      h->mHingeAxis1 = h->mHingeAxis2 = frame.GetAxisX();
      h->mNormalAxis1 = h->mNormalAxis2 = frame.GetAxisY();
      h->mLimitsMin = clamp(d.bend_min, -180.0f, 0.0f) * (pi / 180.0f);
      h->mLimitsMax = clamp(d.bend_max, 0.0f, 180.0f) * (pi / 180.0f);
      part.mToParent = h;
    } else {
      auto *st = new JPH::SwingTwistConstraintSettings();
      st->mPosition1 = st->mPosition2 = anchor;
      st->mTwistAxis1 = st->mTwistAxis2 = frame.GetAxisY();
      st->mPlaneAxis1 = st->mPlaneAxis2 = frame.GetAxisX();
      st->mNormalHalfConeAngle = st->mPlaneHalfConeAngle = clamp(d.swing, 0.0f, 180.0f) * (pi / 180.0f);
      const f32 twist = clamp(d.twist, 0.0f, 180.0f) * (pi / 180.0f);
      st->mTwistMinAngle = -twist;
      st->mTwistMaxAngle = twist;
      part.mToParent = st;
    }
  }
  for (i32 j = 0; j < n; j++) {
    JPH::RagdollSettings::Part &part = settings->mParts[(usize)j];
    part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    part.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.01f) * volume[(usize)j] / std::max(total, 1e-9f);
  }
  settings->Stabilize();
  // Parts of one body never collide with each other, only with the world.
  JPH::Ref<JPH::GroupFilterTable> self = new JPH::GroupFilterTable((JPH::uint)n);
  for (i32 a = 0; a < n; a++)
    for (i32 c = a + 1; c < n; c++)
      self->DisableCollision((JPH::CollisionGroup::SubGroupID)a, (JPH::CollisionGroup::SubGroupID)c);
  for (i32 j = 0; j < n; j++) {
    settings->mParts[(usize)j].mCollisionGroup.SetGroupFilter(self);
    settings->mParts[(usize)j].mCollisionGroup.SetSubGroupID((JPH::CollisionGroup::SubGroupID)j);
  }
  settings->CalculateBodyIndexToConstraintIndex();
  settings->CalculateConstraintIndexToBodyIdxPair();

  const u32 handle = (u32)w.ragdolls.size() + 1;
  JPH::Ref<JPH::Ragdoll> ragdoll = settings->CreateRagdoll(handle, 0, &w.system);
  if (ragdoll == nullptr) {
    NJIN_WARN("physics3d: body limit reached");
    return ragdoll3d_handle{};
  }
  // Built in the rest pose so the limits count from it; now to the start.
  std::vector<JPH::Mat44> pose((usize)n);
  for (i32 j = 0; j < n; j++)
    pose[(usize)j] = unscaled(at * start[(usize)bone_of[(usize)order[(usize)j]]]);
  ragdoll->SetPose(JPH::RVec3::sZero(), pose.data());
  ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
  ragdoll->SetLinearVelocity(jv(desc.velocity));

  ragdoll_slot r{.ragdoll = ragdoll,
                 .alive = true,
                 .parts = std::vector<u32>((usize)n),
                 .part_bone = bone_of,
                 .anchor = std::vector<i32>((usize)bones),
                 .rel = std::vector<JPH::Mat44>((usize)bones),
                 .scale = scale,
                 .shapes = std::move(shapes)};
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  for (i32 j = 0; j < n; j++) {
    const JPH::BodyID id = ragdoll->GetBodyID(j);
    const u32 body = (u32)w.bodies.size() + 1;
    bi.SetUserData(id, body);
    w.handle_by_body[id.GetIndexAndSequenceNumber()] = body;
    w.bodies.push_back(
        body_slot{.id = id, .alive = true, .kinematic = false, .dynamic = true, .user = desc.user, .ragdoll = true});
    r.parts[(usize)order[(usize)j]] = body;
  }
  // Bones without a part keep their start offset from the part above them
  // (the root part for those above every part).
  for (i32 b = 0; b < bones; b++) {
    const i32 a = part_above(b);
    const i32 entry = a >= 0 ? a : root;
    r.anchor[(usize)b] = entry;
    r.rel[(usize)b] = start[(usize)bone_of[(usize)entry]].Inversed() * start[(usize)b];
  }
  w.ragdolls.push_back(std::move(r));
  return ragdoll3d_handle{handle};
}

void ragdoll3d_destroy(context &ctx, ragdoll3d_handle handle) {
  ragdoll_slot *r = ragdoll_of(ctx, handle);
  if (r == nullptr)
    return;
  physics3d_world &w = *ctx.physics3d.world;
  for (u32 h : r->parts)
    forget_body(ctx, w, body3d_handle{h});
  r->ragdoll->RemoveFromPhysicsSystem();
  for (u32 h : r->parts)
    w.bodies[h - 1] = body_slot{};
  *r = ragdoll_slot{}; // the Ragdoll destroys its bodies
}

body3d_handle ragdoll3d_body(const context &ctx, ragdoll3d_handle handle, i32 part) {
  const ragdoll_slot *r = ragdoll_of(ctx, handle);
  if (r == nullptr || part < 0 || part >= (i32)r->parts.size())
    return body3d_handle{};
  return body3d_handle{r->parts[(usize)part]};
}

shape3d ragdoll3d_shape(const context &ctx, ragdoll3d_handle handle, i32 part) {
  const ragdoll_slot *r = ragdoll_of(ctx, handle);
  if (r == nullptr || part < 0 || part >= (i32)r->parts.size())
    return shape3d{.radius = 0.0f};
  const JPH::BodyInterface &bi = ctx.physics3d.world->system.GetBodyInterface();
  const JPH::BodyID id = ctx.physics3d.world->bodies[r->parts[(usize)part] - 1].id;
  const JPH::Quat q = bi.GetRotation(id);
  shape3d s = r->shapes[(usize)part];
  s.position = world_vec(bi.GetPosition(id)) + nv(q * jv(s.position));
  s.rotation = degrees_of(q);
  return s;
}

i32 ragdoll3d_bones(const context &ctx, ragdoll3d_handle handle, const transform3d &transform, bone_pose3d *out,
                    i32 count) {
  const ragdoll_slot *r = ragdoll_of(ctx, handle);
  const i32 bones = r != nullptr ? (i32)r->anchor.size() : 0;
  if (r == nullptr || out == nullptr || count < bones)
    return 0;
  const physics3d_world &w = *ctx.physics3d.world;
  const JPH::BodyInterface &bi = w.system.GetBodyInterface();
  const JPH::Mat44 from_world = draw_mat(transform).Inversed();
  std::vector<JPH::Mat44> part(r->parts.size());
  for (usize i = 0; i < part.size(); i++) {
    const JPH::BodyID id = w.bodies[r->parts[i] - 1].id;
    const JPH::Mat44 body = JPH::Mat44::sRotationTranslation(bi.GetRotation(id), JPH::Vec3(bi.GetPosition(id)));
    part[i] = unscaled(from_world * body * JPH::Mat44::sScale(r->scale));
  }
  for (i32 b = 0; b < bones; b++) {
    const JPH::Mat44 k = part[(usize)r->anchor[(usize)b]] * r->rel[(usize)b];
    out[b] = bone_pose3d{.position = nv(k.GetTranslation()),
                         .x_axis = nv(k.GetAxisX().NormalizedOr(JPH::Vec3::sAxisX())),
                         .y_axis = nv(k.GetAxisY().NormalizedOr(JPH::Vec3::sAxisY())),
                         .z_axis = nv(k.GetAxisZ().NormalizedOr(JPH::Vec3::sAxisZ()))};
  }
  return bones;
}

void physics3d_set_gravity(context &ctx, vec3 gravity) {
  ctx.physics3d.gravity = gravity;
  if (ctx.physics3d.world)
    ctx.physics3d.world->system.SetGravity(jv(gravity));
}

vec3 physics3d_gravity(const context &ctx) { return ctx.physics3d.gravity; }
} // namespace njin
