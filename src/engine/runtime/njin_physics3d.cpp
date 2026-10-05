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
#include <Jolt/Physics/PhysicsSystem.h>
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
  std::vector<u32> touching; // body handles it touched after the last step, sorted
};

struct joint_slot {
  JPH::Ref<JPH::TwoBodyConstraint> constraint;
  bool alive = false;
  joint3d_kind kind = joint3d_hinge;
  body3d_handle a{};
  body3d_handle b{};
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

struct physics3d_world {
  broad_phase_layers broad_phase;
  object_vs_broad_phase object_vs_broad;
  object_pairs pairs;
  JPH::TempAllocatorImpl temp{10 * 1024 * 1024};
  JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
  JPH::PhysicsSystem system;
  contact_listener listener;
  std::vector<body_slot> bodies;         // handle id N is bodies[N - 1]
  std::vector<character_slot> characters; // handle id N is characters[N - 1]
  std::vector<joint_slot> joints;         // handle id N is joints[N - 1]
  std::unordered_map<u32, u32> handle_by_body; // Jolt body id -> njin handle id
  std::unordered_map<u64, pair_state> touching; // pair_key -> the pair
  std::vector<contact3d> contacts; // events of the last step
  character_grid crowd;            // characters against each other
  // Static bodies added since the broad phase was last rebuilt: a town's
  // worth added one by one leaves it slow to query until it is.
  u32 static_added = 0;

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
  }
  // Pushers parted where the step left them overlapping (character3d_desc::push).
  w->crowd.part(2);
  w->system.Update(dt, 1, &w->temp, &w->jobs);
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

void body3d_destroy(context &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return;
  physics3d_world &w = *ctx.physics3d.world;
  // Its joints first: a constraint must not outlive a body it holds.
  for (usize i = 0; i < w.joints.size(); i++)
    if (w.joints[i].alive && (w.joints[i].a.id == handle.id || w.joints[i].b.id == handle.id))
      joint3d_destroy(ctx, joint3d_handle{(u32)(i + 1)});
  // A destroyed body stops touching without an event.
  for (auto it = w.touching.begin(); it != w.touching.end();)
    it = it->second.a.id == handle.id || it->second.b.id == handle.id ? w.touching.erase(it) : std::next(it);
  for (character_slot &c : w.characters)
    std::erase(c.touching, handle.id);
  w.handle_by_body.erase(b->id.GetIndexAndSequenceNumber());
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
  settings->mMass = desc.mass;
  // Supported only by the lower sphere of the capsule, not its side.
  settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -r);
  character_slot slot;
  slot.character = new JPH::CharacterVirtual(
      settings, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z), JPH::Quat::sIdentity(), &w.system);
  slot.alive = true;
  slot.step_height = desc.step_height;
  slot.character->SetCharacterVsCharacterCollision(&w.crowd);
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

void physics3d_set_gravity(context &ctx, vec3 gravity) {
  ctx.physics3d.gravity = gravity;
  if (ctx.physics3d.world)
    ctx.physics3d.world->system.SetGravity(jv(gravity));
}

vec3 physics3d_gravity(const context &ctx) { return ctx.physics3d.gravity; }
} // namespace njin
