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
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
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
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/RegisterTypes.h>
#include <raymath.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

namespace njin {
namespace {
// A position, velocity or size that is NaN or infinite would put a body's
// bounds out of Jolt's broadphase (QuadTree asserts on it in Debug; Release
// reads past its nodes and crashes a step later, far from the cause). Refused
// at the API, said once per function, so the game's bad value shows where it
// came from.
bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool refuse(bool ok, const char *what) {
  if (ok)
    return false;
  static std::vector<const char *> told;
  if (std::find(told.begin(), told.end(), what) == told.end()) {
    told.push_back(what);
    NJIN_WARN("physics3d: %s given a value that is not finite (NaN or infinite): ignored", what);
  }
  return true;
}

// Soft bodies have a layer of their own, and so do the capsules that stand
// for characters against them (proxy): a proxy meets soft bodies only, so
// rays, queries, characters and vehicles never see it.
namespace layers {
constexpr JPH::ObjectLayer still = 0;
constexpr JPH::ObjectLayer moving = 1;
constexpr JPH::ObjectLayer soft = 2;
constexpr JPH::ObjectLayer proxy = 3;
constexpr JPH::uint broad_count = 2; // broad phase: still, and everything else
} // namespace layers

class broad_phase_layers final : public JPH::BroadPhaseLayerInterface {
public:
  JPH::uint GetNumBroadPhaseLayers() const override { return layers::broad_count; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return JPH::BroadPhaseLayer((JPH::BroadPhaseLayer::Type)(layer == layers::still ? 0 : 1));
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return (JPH::BroadPhaseLayer::Type)layer == layers::still ? "still" : "moving";
  }
#endif
};

// Static bodies only need to meet moving ones; proxies only soft bodies,
// which are with the moving ones.
class object_vs_broad_phase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
    return (layer != layers::still && layer != layers::proxy) || (JPH::BroadPhaseLayer::Type)broad == 1;
  }
};

class object_pairs final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    if (a == layers::proxy || b == layers::proxy)
      return (a == layers::proxy ? b : a) == layers::soft;
    if (a == layers::soft && b == layers::soft)
      return false;
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
  bool vehicle = false; // the chassis of a vehicle: the vehicle owns it
  bool has_target = false;
  vec3 target_pos{};
  vec3 target_rot{};
  bool moving_to = false; // MoveKinematic gave it a velocity last step: it stops when no new target comes
};

struct character_slot {
  JPH::Ref<JPH::CharacterVirtual> character;
  bool alive = false;
  bool active = true; // character3d_set_active
  vec3 desired{};
  f32 step_height = 0.3f;
  f32 radius = 0.3f, height = 1.8f;
  JPH::BodyID proxy; // its capsule against soft bodies, while there are any (sync_proxies())
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

// A soft body (softbody3d_create, cloth3d_create): a Jolt soft body, not in
// the body slots (raycasts, contacts and body3d_* only see rigid bodies).
// Pinned vertices have no mass; one the game moves (softbody3d_move_pinned)
// is given the velocity that takes it there in one step, then stopped.
struct soft_slot {
  JPH::BodyID id;
  bool alive = false;
  bool cloth = false; // its model has back faces too
  u64 user = 0;
  f32 drag = 1.0f;
  vec3 wind{};
  std::vector<u32> indices;  // three per face, as Jolt's faces
  f32 vertex_inv_mass = 1.0f; // of a free vertex, for softbody3d_pin
  struct move {
    u32 vertex;
    vec3 to;
  };
  std::vector<move> moves;   // pinned vertices to move this step
  std::vector<u32> moving;   // pinned vertices given a velocity last step
  model_handle model{};      // softbody3d_model, made on first ask
  bool awake = true;         // was active at the last model refresh
};

// A wheeled vehicle (vehicle3d_create): a body slot for the chassis and Jolt's
// VehicleConstraint, a step listener of the physics system.
struct vehicle_slot {
  JPH::Ref<JPH::VehicleConstraint> constraint;
  JPH::Ref<JPH::VehicleCollisionTester> tester;
  bool alive = false;
  body3d_handle body{};
  f32 throttle = 0.0f, steer = 0.0f, brake = 0.0f, handbrake = 0.0f;
  f32 direction = 0.0f; // the way it last accepted to drive: a reversed throttle brakes first
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

// Characters do not stand on or stop at soft bodies: a curtain would be a
// wall. Their proxy pushes the vertices aside instead (sync_proxies()).
class rigid_only final : public JPH::BodyFilter {
public:
  bool ShouldCollideLocked(const JPH::Body &body) const override { return !body.IsSoftBody(); }
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
  std::vector<soft_slot> softs;           // handle id N is softs[N - 1]
  std::vector<vehicle_slot> vehicles;     // handle id N is vehicles[N - 1]
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
  // Loads to carry in the next step (body3d_carry()).
  struct load {
    JPH::BodyID id;
    f32 mass;
    JPH::RVec3 at;
  };
  std::vector<load> loads;

  physics3d_world() {
    system.Init(16384, 0, 16384, 8192, broad_phase, object_vs_broad, pairs);
    listener.world = this;
    system.SetContactListener(&listener);
  }
  ~physics3d_world() {
    crowd.all.clear();
    for (character_slot &c : characters)
      if (c.alive && !c.proxy.IsInvalid()) {
        system.GetBodyInterface().RemoveBody(c.proxy);
        system.GetBodyInterface().DestroyBody(c.proxy);
      }
    characters.clear();
    for (joint_slot &j : joints)
      if (j.alive)
        system.RemoveConstraint(j.constraint);
    joints.clear();
    for (vehicle_slot &v : vehicles)
      if (v.alive) {
        system.RemoveStepListener(v.constraint);
        system.RemoveConstraint(v.constraint);
      }
    vehicles.clear();
    {
      JPH::BodyInterface &bi = system.GetBodyInterface();
      for (soft_slot &s : softs)
        if (s.alive) {
          bi.RemoveBody(s.id);
          bi.DestroyBody(s.id);
        }
      softs.clear(); // their models go with the model store
    }
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
// For the next solve, dynamic body `id` carries `mass` kg at world point `at`
// (a character standing on it, or body3d_carry()): as heavy as body and load
// together, with the load's inertia there and the turn its weight gives about
// the body's centre (gravity already pulls the added mass down). The solver
// then holds a 70 kg person on a 3 kg board as the one heavy thing they are:
// a board on the floor stays put, a seesaw tips, a board leant on a wall slips
// out from under them. The whole weight as a force on the light board alone
// kicked it more each step than the contacts could take back, and the board
// shook. physics3d_step() puts the bodies' own mass back after the solve.
void carry_load(physics3d_world &w, JPH::BodyID id, f32 mass, JPH::RVec3 at, JPH::Vec3 gravity) {
  {
    JPH::BodyLockWrite lock(w.system.GetBodyLockInterface(), id);
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
      return;
    JPH::Body &body = lock.GetBody();
    JPH::MotionProperties *mp = body.GetMotionProperties();
    const JPH::Vec3 inv_inertia = mp->GetInverseInertiaDiagonal();
    if (mp->GetInverseMass() <= 0.0f || inv_inertia.ReduceMin() <= 0.0f)
      return;
    // Where the load is, from the centre of mass in the body's axes, kept a
    // little inside the body: a contact on a board's very edge (a capsule
    // over a gap) can lie outside it.
    const JPH::Vec3 com = body.GetShape()->GetCenterOfMass();
    const JPH::AABox box = body.GetShape()->GetLocalBounds();
    const JPH::Vec3 inset = JPH::Vec3::sMin(box.GetExtent() * 0.5f, JPH::Vec3::sReplicate(0.01f));
    const JPH::Vec3 r =
        JPH::Vec3::sClamp(JPH::Vec3(body.GetInverseCenterOfMassTransform() * at) + com,
                          box.mMin + inset, box.mMax - inset) - com;
    bool seen = false;
    for (const physics3d_world::ridden &q : w.ridden_bodies)
      seen = seen || q.id == id;
    if (!seen)
      w.ridden_bodies.push_back({id, mp->GetInverseMass(), inv_inertia, mp->GetInertiaRotation()});
    // The load as a point mass at r: m r.r E - m r r^T on the inertia.
    const f32 rr = r.Dot(r);
    const JPH::Mat44 point(JPH::Vec4(rr - r.GetX() * r.GetX(), -r.GetY() * r.GetX(), -r.GetZ() * r.GetX(), 0.0f),
                           JPH::Vec4(-r.GetX() * r.GetY(), rr - r.GetY() * r.GetY(), -r.GetZ() * r.GetY(), 0.0f),
                           JPH::Vec4(-r.GetX() * r.GetZ(), -r.GetY() * r.GetZ(), rr - r.GetZ() * r.GetZ(), 0.0f),
                           JPH::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    JPH::MassProperties carried;
    carried.mMass = 1.0f / mp->GetInverseMass() + mass;
    carried.mInertia = mp->GetLocalSpaceInverseInertia().Inversed3x3() + point * mass;
    mp->SetMassProperties(mp->GetAllowedDOFs(), carried);
    body.AddTorque((body.GetRotation() * r).Cross(gravity * mass));
  }
  w.system.GetBodyInterface().ActivateBody(id);
}

soft_slot *soft_of(const context &ctx, softbody3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->softs.size())
    return nullptr;
  soft_slot &s = w->softs[h.id - 1];
  return s.alive ? &s : nullptr;
}

vehicle_slot *vehicle_of(const context &ctx, vehicle3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->vehicles.size())
    return nullptr;
  vehicle_slot &v = w->vehicles[h.id - 1];
  return v.alive ? &v : nullptr;
}

using soft_vertex = JPH::SoftBodyMotionProperties::Vertex;
using soft_vertices = JPH::Array<soft_vertex>;

soft_vertices &vertices_of(JPH::Body &body) {
  return static_cast<JPH::SoftBodyMotionProperties *>(body.GetMotionProperties())->GetVertices();
}

const soft_vertices &vertices_of(const JPH::Body &body) {
  return static_cast<const JPH::SoftBodyMotionProperties *>(body.GetMotionProperties())->GetVertices();
}

// Air on every face of a soft body: the wind's speed through the face pushes
// it along its normal (0.5 rho Cd A v|v|), and still air slows a face moving
// through it. Each vertex's change is held below its own speed relative to
// the wind, so a light cloth in a gust never overtakes the air.
bool blow(soft_slot &s, soft_vertices &verts, f32 dt) {
  constexpr f32 air_density = 1.2f;
  const JPH::Vec3 wind = jv(s.wind);
  std::vector<JPH::Vec3> push(verts.size(), JPH::Vec3::sZero());
  bool any = false;
  for (usize f = 0; f + 2 < s.indices.size(); f += 3) {
    const u32 i0 = s.indices[f], i1 = s.indices[f + 1], i2 = s.indices[f + 2];
    const JPH::Vec3 cross = (verts[i1].mPosition - verts[i0].mPosition).Cross(verts[i2].mPosition - verts[i0].mPosition);
    const f32 twice_area = cross.Length();
    if (twice_area < 1e-10f)
      continue;
    const JPH::Vec3 n = cross / twice_area;
    const JPH::Vec3 v = (verts[i0].mVelocity + verts[i1].mVelocity + verts[i2].mVelocity) / 3.0f;
    const f32 vn = (wind - v).Dot(n);
    const JPH::Vec3 impulse = n * (0.5f * air_density * s.drag * 0.5f * twice_area * vn * std::fabs(vn) * dt / 3.0f);
    if (impulse.LengthSq() < 1e-20f)
      continue;
    for (u32 i : {i0, i1, i2})
      push[i] += impulse;
    any = true;
  }
  if (!any)
    return false;
  for (usize i = 0; i < verts.size(); i++) {
    soft_vertex &v = verts[i];
    const f32 len = push[i].Length();
    if (v.mInvMass <= 0.0f || len < 1e-12f)
      continue;
    const JPH::Vec3 dir = push[i] / len;
    const f32 room = std::max((wind - v.mVelocity).Dot(dir), 0.0f);
    v.mVelocity += dir * std::min(len * v.mInvMass, room);
  }
  return true;
}

// Before the step: pinned vertices go where the game moves them
// (softbody3d_move_pinned) and stop when it no longer does; the air pushes.
void steer_soft_bodies(physics3d_world &w, f32 dt) {
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  for (soft_slot &s : w.softs) {
    if (!s.alive)
      continue;
    bool wake = false;
    {
      JPH::BodyLockWrite lock(w.system.GetBodyLockInterface(), s.id);
      if (!lock.Succeeded())
        continue;
      JPH::Body &body = lock.GetBody();
      soft_vertices &verts = vertices_of(body);
      for (u32 i : s.moving)
        if (i < verts.size() && verts[i].mInvMass <= 0.0f)
          verts[i].mVelocity = JPH::Vec3::sZero();
      s.moving.clear();
      const JPH::RMat44 to_local = body.GetInverseCenterOfMassTransform();
      for (const soft_slot::move &m : s.moves) {
        if (m.vertex >= verts.size() || verts[m.vertex].mInvMass > 0.0f)
          continue;
        const JPH::Vec3 to(to_local * JPH::RVec3(m.to.x, m.to.y, m.to.z));
        verts[m.vertex].mVelocity = (to - verts[m.vertex].mPosition) / dt;
        s.moving.push_back(m.vertex);
        wake = true;
      }
      s.moves.clear();
      const bool windy = s.wind.x != 0.0f || s.wind.y != 0.0f || s.wind.z != 0.0f;
      if (s.drag > 0.0f && (windy || body.IsActive()) && blow(s, verts, dt) && windy)
        wake = true;
    }
    if (wake)
      bi.ActivateBody(s.id);
  }
}

// While soft bodies exist, every active character has a kinematic capsule of
// its shape on the proxy layer, driven to the character each step, so the
// soft body solver pushes the cloth aside (a curtain parts, a ball rolls
// away) within its own iterations. A jump (character3d_set_position) moves it
// there at once rather than sweeping through everything in between.
void sync_proxies(physics3d_world &w, f32 dt) {
  bool any_soft = false;
  for (const soft_slot &s : w.softs)
    any_soft = any_soft || s.alive;
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  for (character_slot &c : w.characters) {
    const bool want = c.alive && c.active && any_soft;
    if (!want) {
      if (!c.proxy.IsInvalid()) {
        bi.RemoveBody(c.proxy);
        bi.DestroyBody(c.proxy);
        c.proxy = JPH::BodyID();
      }
      continue;
    }
    const JPH::RVec3 feet = c.character->GetPosition();
    if (c.proxy.IsInvalid()) {
      JPH::BodyCreationSettings settings(c.character->GetShape(), feet, JPH::Quat::sIdentity(),
                                         JPH::EMotionType::Kinematic, layers::proxy);
      settings.mUserData = 0;
      c.proxy = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);
      continue;
    }
    if (JPH::Vec3(bi.GetPosition(c.proxy) - feet).Length() > 1.0f)
      bi.SetPositionAndRotation(c.proxy, feet, JPH::Quat::sIdentity(), JPH::EActivation::Activate);
    else
      bi.MoveKinematic(c.proxy, feet, JPH::Quat::sIdentity(), dt);
  }
}

// The driver's input for this step. A throttle against the way the car rolls
// brakes it to a stop first, then drives that way.
void drive_vehicles(physics3d_world &w) {
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  for (vehicle_slot &v : w.vehicles) {
    if (!v.alive)
      continue;
    const JPH::BodyID id = w.bodies[v.body.id - 1].id;
    f32 forward = v.throttle, brake = v.brake;
    if (forward != 0.0f && v.direction * forward < 0.0f) {
      const f32 speed = (bi.GetRotation(id).Conjugated() * bi.GetLinearVelocity(id)).GetZ();
      if ((forward > 0.0f && speed < -0.1f) || (forward < 0.0f && speed > 0.1f)) {
        forward = 0.0f;
        brake = 1.0f;
      } else {
        v.direction = forward;
      }
    } else if (forward != 0.0f) {
      v.direction = forward;
    }
    static_cast<JPH::WheeledVehicleController *>(v.constraint->GetController())
        ->SetDriverInput(forward, v.steer, brake, v.handbrake);
    if (forward != 0.0f || v.steer != 0.0f || brake != 0.0f || v.handbrake != 0.0f)
      bi.ActivateBody(id);
  }
}

// World positions and normals (area-weighted over the faces round each
// vertex) of a soft body; `back` adds the back faces' copy (a cloth's model).
void soft_frame(const physics3d_world &w, const soft_slot &s, std::vector<vec3> &pos, std::vector<vec3> &nrm,
                bool back) {
  pos.clear();
  nrm.clear();
  JPH::BodyLockRead lock(w.system.GetBodyLockInterface(), s.id);
  if (!lock.Succeeded())
    return;
  const JPH::Body &body = lock.GetBody();
  const JPH::RMat44 xf = body.GetCenterOfMassTransform();
  const soft_vertices &verts = vertices_of(body);
  std::vector<JPH::Vec3> sum(verts.size(), JPH::Vec3::sZero());
  for (usize f = 0; f + 2 < s.indices.size(); f += 3) {
    const u32 i0 = s.indices[f], i1 = s.indices[f + 1], i2 = s.indices[f + 2];
    const JPH::Vec3 cross = (verts[i1].mPosition - verts[i0].mPosition).Cross(verts[i2].mPosition - verts[i0].mPosition);
    for (u32 i : {i0, i1, i2})
      sum[i] += cross;
  }
  const usize n = verts.size();
  pos.resize(back ? n * 2 : n);
  nrm.resize(pos.size());
  for (usize i = 0; i < n; i++) {
    pos[i] = world_vec(xf * verts[i].mPosition);
    const f32 len = sum[i].Length();
    nrm[i] = len > 1e-12f ? nv(xf.Multiply3x3(sum[i] / len)) : vec3{0.0f, 1.0f, 0.0f};
    if (back) {
      pos[n + i] = pos[i];
      nrm[n + i] = nrm[i] * -1.0f;
    }
  }
}

// After the step: soft bodies that moved bring their model along.
void refresh_soft_models(context &ctx, physics3d_world &w) {
  std::vector<vec3> pos, nrm;
  for (soft_slot &s : w.softs) {
    if (!s.alive || s.model.id == 0)
      continue;
    // Once more on the step it falls asleep, so the model has its last shape.
    const bool active = w.system.GetBodyInterface().IsActive(s.id);
    const bool refresh = active || s.awake;
    s.awake = active;
    if (!refresh)
      continue;
    soft_frame(w, s, pos, nrm, s.cloth);
    model_store_update_vertices(ctx.model, s.model, pos.data(), nrm.data(), (u32)pos.size());
  }
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
    if (slot != nullptr && slot->kinematic &&
        !refuse(finite3(t.position) && finite3(t.rotation), "a kinematic body3d component's transform3d")) {
      slot->has_target = true;
      slot->target_pos = t.position;
      slot->target_rot = t.rotation;
    }
  }
  for (body_slot &b : w->bodies) {
    if (!b.alive || !b.kinematic)
      continue;
    if (b.has_target) {
      bi.MoveKinematic(b.id, jv(b.target_pos), quat_of(b.target_rot), dt);
      b.has_target = false;
      b.moving_to = true;
    } else if (b.moving_to) {
      // MoveKinematic reaches its target by setting the velocity for one step,
      // and Jolt keeps that velocity: with no new target the body would fly
      // on for ever (a door opened once, kilometres away minutes later, its
      // bounds past the broadphase's range: a crash). It stops where it got to.
      bi.SetLinearAndAngularVelocity(b.id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
      b.moving_to = false;
    }
  }
  if (w->static_added >= 32) {
    w->system.OptimizeBroadPhase();
    w->static_added = 0;
  }
  const JPH::Vec3 gravity = jv(ctx.physics3d.gravity);
  steer_soft_bodies(*w, dt);
  w->crowd.rebuild(dt);
  const rigid_only rigid;
  for (character_slot &c : w->characters) {
    if (!c.alive || !c.active)
      continue;
    c.character->UpdateGroundVelocity();
    c.character->SetLinearVelocity(jv(c.desired));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, c.step_height, 0.0f);
    c.character->ExtendedUpdate(dt, gravity, settings, w->system.GetDefaultBroadPhaseLayerFilter(layers::moving),
                                w->system.GetDefaultLayerFilter(layers::moving), rigid, {}, w->temp);
    // Its weight on what it stands on: a dynamic body there carries it for the
    // solve (carry_load()), shared among every point it stands on: one foot on
    // each of two boards presses both.
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
          carry_load(*w, k.mBodyB, share, k.mPosition, gravity);
        }
      }
    }
  }
  // Loads the game hung on bodies for this step (body3d_carry()).
  for (const physics3d_world::load &l : w->loads)
    carry_load(*w, l.id, l.mass, l.at, gravity);
  w->loads.clear();
  // Pushers parted where the step left them overlapping (character3d_desc::push).
  w->crowd.part(2);
  sync_proxies(*w, dt);
  drive_vehicles(*w);
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
  refresh_soft_models(ctx, *w);
}

body3d_handle body3d_create(context &ctx, const body3d_desc &desc) {
  if (refuse(finite3(desc.position) && finite3(desc.rotation) && finite3(desc.size), "body3d_create"))
    return body3d_handle{};
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
  if (b->vehicle) {
    NJIN_WARN("physics3d: body3d_destroy on the body of a vehicle: use vehicle3d_destroy");
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
  if (b == nullptr || refuse(finite3(position) && finite3(rotation), "body3d_set_position"))
    return;
  ctx.physics3d.world->system.GetBodyInterface().SetPositionAndRotation(
      b->id, JPH::RVec3(position.x, position.y, position.z), quat_of(rotation), JPH::EActivation::Activate);
  b->has_target = false;
}

void body3d_move_kinematic(context &ctx, body3d_handle handle, vec3 position, vec3 rotation) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr || refuse(finite3(position) && finite3(rotation), "body3d_move_kinematic"))
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
  if (b != nullptr && !refuse(finite3(velocity), "body3d_set_velocity")) {
    ctx.physics3d.world->system.GetBodyInterface().SetLinearVelocity(b->id, jv(velocity));
    b->moving_to = false; // a velocity the game set is kept
  }
}

void body3d_add_impulse(context &ctx, body3d_handle handle, vec3 impulse) {
  body_slot *b = body_of(ctx, handle);
  if (b != nullptr && !refuse(finite3(impulse), "body3d_add_impulse"))
    ctx.physics3d.world->system.GetBodyInterface().AddImpulse(b->id, jv(impulse));
}

void body3d_carry(context &ctx, body3d_handle handle, f32 mass, vec3 point) {
  body_slot *b = body_of(ctx, handle);
  if (b != nullptr && mass > 0.0f)
    ctx.physics3d.world->loads.push_back({b->id, mass, jv(point)});
}

u64 body3d_user(const context &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  return b != nullptr ? b->user : 0;
}

character3d_handle character3d_create(context &ctx, const character3d_desc &desc) {
  if (refuse(finite3(desc.position), "character3d_create"))
    return character3d_handle{};
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
  slot.radius = r;
  slot.height = half_height * 2.0f;
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
    if (!c->proxy.IsInvalid()) {
      JPH::BodyInterface &bi = ctx.physics3d.world->system.GetBodyInterface();
      bi.RemoveBody(c->proxy);
      bi.DestroyBody(c->proxy);
    }
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
  if (character_slot *c = character_of(ctx, handle); c != nullptr && !refuse(finite3(velocity), "character3d_set_velocity"))
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
  if (refuse(finite3(position), "character3d_set_position"))
    return;
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
// And through soft bodies, which have no body handle to report.
class not_sensor final : public JPH::BodyFilter {
public:
  bool ShouldCollideLocked(const JPH::Body &body) const override {
    return !body.IsSensor() && !body.IsSoftBody() && body.GetObjectLayer() != layers::proxy;
  }
};
} // namespace

namespace {
// Everything but sensors, soft bodies and one body.
class not_sensor_or final : public JPH::BodyFilter {
public:
  JPH::BodyID skip;
  bool ShouldCollideLocked(const JPH::Body &body) const override {
    return !body.IsSensor() && !body.IsSoftBody() && body.GetObjectLayer() != layers::proxy && body.GetID() != skip;
  }
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

namespace {
// A soft body's surface while it is being built: vertices in its own axes and
// three indices per face. Box lattices add their inner edges and tetrahedra
// to `shared` themselves.
struct soft_build {
  std::vector<JPH::Float3> verts;
  std::vector<u32> tris;
  bool solid = false; // edges and volumes are already in the shared settings
};

f32 six_volume(const soft_build &b) {
  f32 v = 0.0f;
  for (usize f = 0; f + 2 < b.tris.size(); f += 3)
    v += JPH::Vec3(b.verts[b.tris[f]]).Cross(JPH::Vec3(b.verts[b.tris[f + 1]])).Dot(JPH::Vec3(b.verts[b.tris[f + 2]]));
  return v;
}

// Faces outward: a closed surface built the other way round is turned over.
void face_outward(soft_build &b) {
  if (six_volume(b) < 0.0f)
    for (usize f = 0; f + 2 < b.tris.size(); f += 3)
      std::swap(b.tris[f + 1], b.tris[f + 2]);
}

// A solid lattice (as Jolt's SoftBodySharedSettings::sCreateCube, with any
// size per axis): every edge of the 6 tetrahedra of each cell (so it resists
// shear, not only stretch), the tetrahedra as volume constraints, and the six
// sides as faces.
void build_box(soft_build &b, JPH::SoftBodySharedSettings &shared, vec3 size, i32 detail) {
  const f32 longest = std::max(size.x, std::max(size.y, size.z));
  const i32 d = std::clamp(detail > 0 ? detail : 5, 2, 16);
  const f32 spacing = longest / (f32)(d - 1);
  const i32 n[3] = {std::clamp((i32)std::lround(size.x / spacing) + 1, 2, 16),
                    std::clamp((i32)std::lround(size.y / spacing) + 1, 2, 16),
                    std::clamp((i32)std::lround(size.z / spacing) + 1, 2, 16)};
  const f32 s[3] = {size.x, size.y, size.z};
  auto at = [&](i32 x, i32 y, i32 z) { return (u32)(x + y * n[0] + z * n[0] * n[1]); };
  for (i32 z = 0; z < n[2]; z++)
    for (i32 y = 0; y < n[1]; y++)
      for (i32 x = 0; x < n[0]; x++) {
        const i32 c[3] = {x, y, z};
        JPH::Float3 p;
        f32 *q[3] = {&p.x, &p.y, &p.z};
        for (i32 k = 0; k < 3; k++)
          *q[k] = -0.5f * s[k] + s[k] * (f32)c[k] / (f32)(n[k] - 1);
        b.verts.push_back(p);
      }
  constexpr i32 tetra[6][4][3] = {{{0, 0, 0}, {0, 1, 1}, {0, 0, 1}, {1, 1, 1}}, {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {1, 1, 1}},
                                  {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}}, {{0, 0, 0}, {1, 0, 1}, {1, 0, 0}, {1, 1, 1}},
                                  {{0, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}};
  std::map<std::pair<u32, u32>, bool> edges;
  for (i32 z = 0; z + 1 < n[2]; z++)
    for (i32 y = 0; y + 1 < n[1]; y++)
      for (i32 x = 0; x + 1 < n[0]; x++)
        for (const auto &t : tetra) {
          u32 v[4];
          for (i32 i = 0; i < 4; i++)
            v[i] = at(x + t[i][0], y + t[i][1], z + t[i][2]);
          shared.mVolumeConstraints.push_back(JPH::SoftBodySharedSettings::Volume(v[0], v[1], v[2], v[3]));
          for (i32 i = 0; i < 4; i++)
            for (i32 j = i + 1; j < 4; j++)
              edges[{std::min(v[i], v[j]), std::max(v[i], v[j])}] = true;
        }
  for (const auto &e : edges)
    shared.mEdgeConstraints.push_back(JPH::SoftBodySharedSettings::Edge(e.first.first, e.first.second));
  // The sides: for the side across axis a, U x V = the axis.
  for (i32 a = 0; a < 3; a++) {
    const i32 u = (a + 1) % 3, v = (a + 2) % 3;
    for (i32 side = 0; side < 2; side++) {
      const i32 fixed = side == 0 ? 0 : n[a] - 1;
      for (i32 j = 0; j + 1 < n[v]; j++)
        for (i32 i = 0; i + 1 < n[u]; i++) {
          auto corner = [&](i32 di, i32 dj) {
            i32 c[3];
            c[a] = fixed;
            c[u] = i + di;
            c[v] = j + dj;
            return at(c[0], c[1], c[2]);
          };
          const u32 q[4] = {corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1)};
          if (side == 1)
            b.tris.insert(b.tris.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
          else
            b.tris.insert(b.tris.end(), {q[0], q[2], q[1], q[0], q[3], q[2]});
        }
    }
  }
  b.solid = true;
}

// A geodesic sphere: an icosahedron, each face split in four `subdiv` times.
void build_sphere(soft_build &b, f32 radius, i32 subdiv) {
  const f32 t = (1.0f + std::sqrt(5.0f)) * 0.5f;
  std::vector<JPH::Vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                              {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  std::vector<u32> f = {0, 11, 5, 0, 5,  1,  0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7,
                        6, 7, 1,  8, 3, 9,  4,  3, 4, 2, 3, 2,  6,  3, 6,  8,  3, 8, 9, 4, 9,  5,  2,  4,  11, 6,
                        2, 10, 8, 6, 7, 9,  8,  1};
  for (i32 k = 0; k < std::clamp(subdiv, 1, 4); k++) {
    std::map<std::pair<u32, u32>, u32> mid;
    auto middle = [&](u32 a, u32 c) {
      const auto key = std::make_pair(std::min(a, c), std::max(a, c));
      const auto it = mid.find(key);
      if (it != mid.end())
        return it->second;
      v.push_back((v[a] + v[c]) * 0.5f);
      return mid[key] = (u32)v.size() - 1;
    };
    std::vector<u32> next;
    for (usize i = 0; i + 2 < f.size(); i += 3) {
      const u32 a = f[i], c = f[i + 1], e = f[i + 2];
      const u32 ab = middle(a, c), ce = middle(c, e), ea = middle(e, a);
      next.insert(next.end(), {a, ab, ea, c, ce, ab, e, ea, ce, ab, ce, ea});
    }
    f = std::move(next);
  }
  for (const JPH::Vec3 &p : v) {
    const JPH::Vec3 q = p.Normalized() * radius;
    b.verts.push_back(JPH::Float3(q.GetX(), q.GetY(), q.GetZ()));
  }
  b.tris = std::move(f);
}

// A surface from triangles, vertices at the same place made one (a model's
// UV seams would otherwise split it). `map` gets the welded index of each
// input vertex.
void build_welded(soft_build &b, const std::vector<JPH::Float3> &in, const std::vector<u32> &tris, std::vector<u32> &map) {
  JPH::AABox box;
  for (const JPH::Float3 &p : in)
    box.Encapsulate(JPH::Vec3(p));
  const f32 cell = std::max(box.GetSize().Length() * 1e-5f, 1e-6f);
  std::unordered_map<u64, u32> seen;
  map.resize(in.size());
  for (usize i = 0; i < in.size(); i++) {
    const u64 key = ((u64)(u32)(i32)std::lround(in[i].x / cell) * 73856093ull) ^
                    ((u64)(u32)(i32)std::lround(in[i].y / cell) * 19349663ull) ^
                    ((u64)(u32)(i32)std::lround(in[i].z / cell) * 83492791ull);
    const auto it = seen.find(key);
    if (it != seen.end() && JPH::Vec3(b.verts[it->second]).IsClose(JPH::Vec3(in[i]), cell * cell * 4.0f)) {
      map[i] = it->second;
      continue;
    }
    map[i] = (u32)b.verts.size();
    seen[key] = map[i];
    b.verts.push_back(in[i]);
  }
  for (usize f = 0; f + 2 < tris.size(); f += 3) {
    const u32 a = map[tris[f]], c = map[tris[f + 1]], e = map[tris[f + 2]];
    if (a != c && c != e && e != a)
      b.tris.insert(b.tris.end(), {a, c, e});
  }
}

// A 0..1 stiffness as XPBD compliance: how soft the constraint is next to a
// vertex's own correction in one sub-step (0 rigid, 10 very soft), so a light
// cloth and a heavy mattress feel alike at the same number.
f32 compliance_of(f32 stiffness, f32 inv_mass) {
  const f32 s = clamp(stiffness, 0.0f, 1.0f);
  if (s >= 1.0f)
    return 0.0f;
  constexpr f32 sub_dt = (1.0f / 60.0f) / 5.0f; // a 60 Hz step in Jolt's 5 iterations
  const f32 k = 1.0f - s;
  return 10.0f * k * k * k * inv_mass * sub_dt * sub_dt;
}

struct soft_params {
  vec3 position, rotation;
  f32 mass, stiffness, bend, pressure, friction, restitution, damping, drag, radius;
  bool cloth;
  u64 user;
  std::vector<u32> pinned; // indices into build.verts
};

softbody3d_handle add_soft_body(physics3d_world &w, soft_build &b, JPH::Ref<JPH::SoftBodySharedSettings> shared,
                                const soft_params &p) {
  const usize n = b.verts.size();
  if (n < 3 || b.tris.size() < 3) {
    NJIN_WARN("physics3d: a soft body needs at least one triangle");
    return softbody3d_handle{};
  }
  const f32 inv_mass = (f32)n / std::max(p.mass, 1e-4f);
  for (usize i = 0; i < n; i++)
    shared->mVertices.push_back(JPH::SoftBodySharedSettings::Vertex(b.verts[i], JPH::Float3(0, 0, 0), inv_mass));
  for (u32 i : p.pinned)
    if (i < n)
      shared->mVertices[i].mInvMass = 0.0f;
  for (usize f = 0; f + 2 < b.tris.size(); f += 3)
    shared->AddFace(JPH::SoftBodySharedSettings::Face(b.tris[f], b.tris[f + 1], b.tris[f + 2]));
  const f32 edge = compliance_of(p.stiffness, inv_mass);
  if (b.solid) {
    for (JPH::SoftBodySharedSettings::Edge &e : shared->mEdgeConstraints)
      e.mCompliance = edge;
    for (JPH::SoftBodySharedSettings::Volume &v : shared->mVolumeConstraints)
      v.mCompliance = edge;
    shared->CalculateEdgeLengths();
    shared->CalculateVolumeConstraintVolumes();
  } else {
    const f32 bend = p.bend > 0.0f ? compliance_of(p.bend, inv_mass) : FLT_MAX;
    const bool anchored = !p.pinned.empty();
    const JPH::SoftBodySharedSettings::VertexAttributes attributes(
        edge, edge, bend,
        p.cloth && anchored ? JPH::SoftBodySharedSettings::ELRAType::GeodesicDistance
                            : JPH::SoftBodySharedSettings::ELRAType::None);
    shared->CreateConstraints(&attributes, 1, JPH::SoftBodySharedSettings::EBendType::Dihedral);
  }
  shared->Optimize();
  JPH::SoftBodyCreationSettings settings(shared, JPH::RVec3(p.position.x, p.position.y, p.position.z),
                                         quat_of(p.rotation), layers::soft);
  settings.mFriction = clamp(p.friction, 0.0f, 1.0f);
  settings.mRestitution = clamp(p.restitution, 0.0f, 1.0f);
  settings.mLinearDamping = std::max(p.damping, 0.0f);
  // Jolt's pressure is n R T: the pressure at rest times the rest volume.
  settings.mPressure = p.pressure > 0.0f ? p.pressure * std::fabs(six_volume(b)) / 6.0f : 0.0f;
  settings.mVertexRadius = std::max(p.radius, 0.0f);
  settings.mFacesDoubleSided = p.cloth;
  settings.mUserData = 0; // not a body slot: handle_of() finds nothing
  const JPH::BodyID id = w.system.GetBodyInterface().CreateAndAddSoftBody(settings, JPH::EActivation::Activate);
  if (id.IsInvalid()) {
    NJIN_WARN("physics3d: body limit reached");
    return softbody3d_handle{};
  }
  soft_slot s;
  s.id = id;
  s.alive = true;
  s.cloth = p.cloth;
  s.user = p.user;
  s.drag = std::max(p.drag, 0.0f);
  s.vertex_inv_mass = inv_mass;
  // Jolt's faces, which Optimize() may have reordered, give the surface.
  for (const JPH::SoftBodySharedSettings::Face &f : shared->mFaces)
    s.indices.insert(s.indices.end(), {f.mVertex[0], f.mVertex[1], f.mVertex[2]});
  w.softs.push_back(std::move(s));
  return softbody3d_handle{(u32)w.softs.size()};
}
} // namespace

softbody3d_handle softbody3d_create(context &ctx, const softbody3d_desc &desc) {
  if (refuse(finite3(desc.position) && finite3(desc.rotation) && finite3(desc.size) && finite3(desc.scale) &&
                 std::isfinite(desc.radius) && std::isfinite(desc.mass) && std::isfinite(desc.pressure),
             "softbody3d_create"))
    return softbody3d_handle{};
  physics3d_world &w = world_of(ctx);
  soft_build b;
  JPH::Ref<JPH::SoftBodySharedSettings> shared = new JPH::SoftBodySharedSettings;
  soft_params p{.position = desc.position,
                .rotation = desc.rotation,
                .mass = desc.mass,
                .stiffness = desc.stiffness,
                .bend = desc.bend,
                .pressure = desc.pressure,
                .friction = desc.friction,
                .restitution = desc.restitution,
                .damping = desc.damping,
                .drag = desc.drag,
                .radius = 0.01f,
                .cloth = false,
                .user = desc.user,
                .pinned = {}};
  std::vector<u32> map;
  switch (desc.kind) {
  case softbody3d_box: {
    const vec3 size{std::max(desc.size.x, 0.02f), std::max(desc.size.y, 0.02f), std::max(desc.size.z, 0.02f)};
    build_box(b, *shared, size, desc.detail);
    break;
  }
  case softbody3d_sphere:
    build_sphere(b, std::max(desc.radius, 0.01f), desc.detail > 0 ? desc.detail : 2);
    break;
  case softbody3d_mesh: {
    std::vector<JPH::Float3> in;
    std::vector<u32> tris;
    if (desc.model.id != 0) {
      const model_slot *slot = model_slot_of(ctx.model, desc.model);
      if (slot == nullptr) {
        NJIN_WARN("physics3d: softbody3d_desc::model is not a loaded model");
        return softbody3d_handle{};
      }
      const Matrix xf = MatrixMultiply(slot->model.transform, MatrixScale(desc.scale.x, desc.scale.y, desc.scale.z));
      for (i32 m = 0; m < slot->model.meshCount; m++) {
        const Mesh &mesh = slot->model.meshes[m];
        if (mesh.vertices == nullptr)
          continue;
        const u32 base = (u32)in.size();
        for (i32 v = 0; v < mesh.vertexCount; v++) {
          const Vector3 q =
              Vector3Transform({mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]}, xf);
          in.push_back(JPH::Float3(q.x, q.y, q.z));
        }
        for (i32 t = 0; t < mesh.triangleCount * 3; t++)
          tris.push_back(base + (mesh.indices != nullptr ? (u32)mesh.indices[t] : (u32)t));
      }
    } else if (desc.mesh.positions != nullptr) {
      for (u32 i = 0; i < desc.mesh.vertex_count; i++)
        in.push_back(JPH::Float3(desc.mesh.positions[i].x, desc.mesh.positions[i].y, desc.mesh.positions[i].z));
      const u32 count = desc.mesh.indices != nullptr ? desc.mesh.index_count : desc.mesh.vertex_count;
      for (u32 i = 0; i < count; i++) {
        const u32 k = desc.mesh.indices != nullptr ? desc.mesh.indices[i] : i;
        if (k >= desc.mesh.vertex_count) {
          NJIN_WARN("physics3d: softbody3d_desc::mesh index %u is past its %u vertices", k, desc.mesh.vertex_count);
          return softbody3d_handle{};
        }
        tris.push_back(k);
      }
      tris.resize(tris.size() / 3 * 3);
    }
    if (in.empty() || tris.size() < 3) {
      NJIN_WARN("physics3d: softbody3d_mesh needs a mesh or a model with triangles");
      return softbody3d_handle{};
    }
    build_welded(b, in, tris, map);
    break;
  }
  default:
    NJIN_WARN("physics3d: softbody3d_desc::kind %d is not a soft body shape", (int)desc.kind);
    return softbody3d_handle{};
  }
  if (desc.kind != softbody3d_mesh)
    face_outward(b);
  for (u32 i = 0; i < desc.pinned_count && desc.pinned != nullptr; i++) {
    const u32 k = desc.pinned[i];
    if (desc.kind == softbody3d_mesh)
      p.pinned.push_back(k < map.size() ? map[k] : ~0u);
    else
      p.pinned.push_back(k);
  }
  return add_soft_body(w, b, shared, p);
}

softbody3d_handle cloth3d_create(context &ctx, const cloth3d_desc &desc) {
  if (refuse(finite3(desc.position) && finite3(desc.rotation) && std::isfinite(desc.size.x) &&
                 std::isfinite(desc.size.y) && std::isfinite(desc.mass),
             "cloth3d_create"))
    return softbody3d_handle{};
  if (desc.columns < 1 || desc.rows < 1 || desc.columns > 180 || desc.rows > 180) {
    NJIN_WARN("physics3d: cloth3d_create: columns and rows must be 1..180 (%d x %d)", desc.columns, desc.rows);
    return softbody3d_handle{};
  }
  physics3d_world &w = world_of(ctx);
  soft_build b;
  const i32 cols = desc.columns + 1, rows = desc.rows + 1;
  const f32 width = std::max(desc.size.x, 0.01f), height = std::max(desc.size.y, 0.01f);
  for (i32 r = 0; r < rows; r++)
    for (i32 c = 0; c < cols; c++)
      b.verts.push_back(JPH::Float3(-0.5f * width + width * (f32)c / (f32)desc.columns,
                                    0.5f * height - height * (f32)r / (f32)desc.rows, 0.0f));
  for (i32 r = 0; r + 1 < rows; r++)
    for (i32 c = 0; c + 1 < cols; c++) {
      const u32 i00 = (u32)(r * cols + c), i01 = i00 + 1, i10 = i00 + (u32)cols, i11 = i10 + 1;
      if ((r + c) % 2 == 0)
        b.tris.insert(b.tris.end(), {i10, i11, i01, i10, i01, i00});
      else
        b.tris.insert(b.tris.end(), {i10, i11, i00, i11, i01, i00});
    }
  soft_params p{.position = desc.position,
                .rotation = desc.rotation,
                .mass = desc.mass,
                .stiffness = desc.stiffness,
                .bend = desc.bend,
                .pressure = 0.0f,
                .friction = desc.friction,
                .restitution = 0.0f,
                .damping = desc.damping,
                .drag = desc.drag,
                .radius = desc.thickness,
                .cloth = true,
                .user = desc.user,
                .pinned = {}};
  for (i32 r = 0; r < rows; r++)
    for (i32 c = 0; c < cols; c++)
      if (((desc.pin_edges & cloth3d_top) && r == 0) || ((desc.pin_edges & cloth3d_bottom) && r == rows - 1) ||
          ((desc.pin_edges & cloth3d_left) && c == 0) || ((desc.pin_edges & cloth3d_right) && c == cols - 1))
        p.pinned.push_back((u32)(r * cols + c));
  for (u32 i = 0; i < desc.pinned_count && desc.pinned != nullptr; i++)
    p.pinned.push_back(desc.pinned[i]);
  return add_soft_body(w, b, new JPH::SoftBodySharedSettings, p);
}

void softbody3d_destroy(context &ctx, softbody3d_handle handle) {
  soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return;
  JPH::BodyInterface &bi = ctx.physics3d.world->system.GetBodyInterface();
  bi.RemoveBody(s->id);
  bi.DestroyBody(s->id);
  if (s->model.id != 0)
    model_store_unload(ctx.model, s->model);
  *s = soft_slot{};
}

i32 softbody3d_vertex_count(const context &ctx, softbody3d_handle handle) {
  const soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return 0;
  JPH::BodyLockRead lock(ctx.physics3d.world->system.GetBodyLockInterface(), s->id);
  return lock.Succeeded() ? (i32)vertices_of(lock.GetBody()).size() : 0;
}

i32 softbody3d_vertices(const context &ctx, softbody3d_handle handle, vec3 *out, i32 count) {
  const soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return 0;
  JPH::BodyLockRead lock(ctx.physics3d.world->system.GetBodyLockInterface(), s->id);
  if (!lock.Succeeded())
    return 0;
  const JPH::RMat44 xf = lock.GetBody().GetCenterOfMassTransform();
  const soft_vertices &verts = vertices_of(lock.GetBody());
  if (out != nullptr)
    for (i32 i = 0; i < count && i < (i32)verts.size(); i++)
      out[i] = world_vec(xf * verts[(usize)i].mPosition);
  return (i32)verts.size();
}

i32 softbody3d_normals(const context &ctx, softbody3d_handle handle, vec3 *out, i32 count) {
  const soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return 0;
  std::vector<vec3> pos, nrm;
  soft_frame(*ctx.physics3d.world, *s, pos, nrm, false);
  if (out != nullptr)
    for (i32 i = 0; i < count && i < (i32)nrm.size(); i++)
      out[i] = nrm[(usize)i];
  return (i32)nrm.size();
}

i32 softbody3d_indices(const context &ctx, softbody3d_handle handle, u32 *out, i32 count) {
  const soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return 0;
  if (out != nullptr)
    for (i32 i = 0; i < count && i < (i32)s->indices.size(); i++)
      out[i] = s->indices[(usize)i];
  return (i32)s->indices.size();
}

model_handle softbody3d_model(context &ctx, softbody3d_handle handle) {
  soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return model_handle{};
  if (s->model.id != 0)
    return s->model;
  std::vector<vec3> pos, nrm;
  soft_frame(*ctx.physics3d.world, *s, pos, nrm, s->cloth);
  if (pos.empty())
    return model_handle{};
  if (pos.size() > 65535) {
    NJIN_WARN("physics3d: softbody3d_model: %u vertices, at most 65535 in a model", (u32)pos.size());
    return model_handle{};
  }
  std::vector<u32> indices = s->indices;
  if (s->cloth) {
    const u32 n = (u32)(pos.size() / 2);
    for (usize f = 0; f + 2 < s->indices.size(); f += 3)
      indices.insert(indices.end(), {s->indices[f] + n, s->indices[f + 2] + n, s->indices[f + 1] + n});
  }
  s->model = model_create(ctx, mesh3d_data{.positions = pos.data(),
                                           .normals = nrm.data(),
                                           .vertex_count = (u32)pos.size(),
                                           .indices = indices.data(),
                                           .index_count = (u32)indices.size()});
  return s->model;
}

i32 softbody3d_nearest(const context &ctx, softbody3d_handle handle, vec3 point) {
  const soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return -1;
  JPH::BodyLockRead lock(ctx.physics3d.world->system.GetBodyLockInterface(), s->id);
  if (!lock.Succeeded())
    return -1;
  const JPH::Vec3 local(lock.GetBody().GetInverseCenterOfMassTransform() * JPH::RVec3(point.x, point.y, point.z));
  const soft_vertices &verts = vertices_of(lock.GetBody());
  i32 best = -1;
  f32 best_d = FLT_MAX;
  for (usize i = 0; i < verts.size(); i++) {
    const f32 d = (verts[i].mPosition - local).LengthSq();
    if (d < best_d) {
      best_d = d;
      best = (i32)i;
    }
  }
  return best;
}

void softbody3d_pin(context &ctx, softbody3d_handle handle, i32 vertex, bool pinned) {
  soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr || vertex < 0)
    return;
  physics3d_world &w = *ctx.physics3d.world;
  {
    JPH::BodyLockWrite lock(w.system.GetBodyLockInterface(), s->id);
    if (!lock.Succeeded())
      return;
    soft_vertices &verts = vertices_of(lock.GetBody());
    if ((usize)vertex >= verts.size())
      return;
    verts[(usize)vertex].mInvMass = pinned ? 0.0f : s->vertex_inv_mass;
    if (pinned)
      verts[(usize)vertex].mVelocity = JPH::Vec3::sZero();
  }
  w.system.GetBodyInterface().ActivateBody(s->id);
}

void softbody3d_move_pinned(context &ctx, softbody3d_handle handle, i32 vertex, vec3 position) {
  soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr || vertex < 0 || refuse(finite3(position), "softbody3d_move_pinned"))
    return;
  for (soft_slot::move &m : s->moves)
    if (m.vertex == (u32)vertex) {
      m.to = position;
      return;
    }
  s->moves.push_back({(u32)vertex, position});
}

void softbody3d_set_wind(context &ctx, softbody3d_handle handle, vec3 wind) {
  soft_slot *s = soft_of(ctx, handle);
  if (s != nullptr && !refuse(finite3(wind), "softbody3d_set_wind"))
    s->wind = wind;
}

void softbody3d_add_impulse(context &ctx, softbody3d_handle handle, vec3 impulse) {
  soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr || refuse(finite3(impulse), "softbody3d_add_impulse"))
    return;
  physics3d_world &w = *ctx.physics3d.world;
  {
    JPH::BodyLockWrite lock(w.system.GetBodyLockInterface(), s->id);
    if (!lock.Succeeded())
      return;
    soft_vertices &verts = vertices_of(lock.GetBody());
    f32 mass = 0.0f;
    for (const soft_vertex &v : verts)
      if (v.mInvMass > 0.0f)
        mass += 1.0f / v.mInvMass;
    if (mass <= 0.0f)
      return;
    const JPH::Vec3 dv = jv(impulse) / mass;
    for (soft_vertex &v : verts)
      if (v.mInvMass > 0.0f)
        v.mVelocity += dv;
  }
  w.system.GetBodyInterface().ActivateBody(s->id);
}

vec3 softbody3d_position(const context &ctx, softbody3d_handle handle) {
  const soft_slot *s = soft_of(ctx, handle);
  if (s == nullptr)
    return vec3{};
  JPH::BodyLockRead lock(ctx.physics3d.world->system.GetBodyLockInterface(), s->id);
  if (!lock.Succeeded())
    return vec3{};
  const JPH::RMat44 xf = lock.GetBody().GetCenterOfMassTransform();
  const soft_vertices &verts = vertices_of(lock.GetBody());
  JPH::Vec3 sum = JPH::Vec3::sZero();
  for (const soft_vertex &v : verts)
    sum += v.mPosition;
  return verts.empty() ? vec3{} : world_vec(xf * (sum / (f32)verts.size()));
}

u64 softbody3d_user(const context &ctx, softbody3d_handle handle) {
  const soft_slot *s = soft_of(ctx, handle);
  return s != nullptr ? s->user : 0;
}

vehicle3d_handle vehicle3d_create(context &ctx, const vehicle3d_desc &desc) {
  if (refuse(finite3(desc.position) && finite3(desc.rotation) && finite3(desc.size) && finite3(desc.scale) &&
                 finite3(desc.center_of_mass) && std::isfinite(desc.mass),
             "vehicle3d_create"))
    return vehicle3d_handle{};
  physics3d_world &w = world_of(ctx);
  body3d_desc chassis{.shape = shape3d_box, .size = desc.size, .motion = body3d_dynamic, .model = desc.model,
                      .scale = desc.scale};
  JPH::RefConst<JPH::Shape> shape = desc.model.id != 0 ? make_model_shape(ctx, chassis) : make_shape(chassis);
  if (shape == nullptr)
    return vehicle3d_handle{};
  JPH::ShapeSettings::ShapeResult offset = JPH::OffsetCenterOfMassShapeSettings(jv(desc.center_of_mass), shape).Create();
  if (offset.HasError()) {
    NJIN_WARN("physics3d: vehicle3d_create: %s", offset.GetError().c_str());
    return vehicle3d_handle{};
  }
  // The wheels: the game's, or four at the bottom corners of the box.
  std::vector<vehicle3d_wheel> wheels;
  if (desc.wheels != nullptr) {
    wheels.assign(desc.wheels, desc.wheels + desc.wheel_count);
  } else {
    const f32 x = std::max(desc.size.x * 0.5f - desc.wheel_width * 0.5f, 0.05f);
    const f32 z = std::max(desc.size.z * 0.5f - desc.wheel_radius * 1.2f, 0.05f);
    const f32 y = -desc.size.y * 0.5f;
    for (const f32 sz : {z, -z})
      for (const f32 sx : {x, -x})
        wheels.push_back(vehicle3d_wheel{.position = {sx, y, sz},
                                         .radius = desc.wheel_radius,
                                         .width = desc.wheel_width,
                                         .steer = sz > 0.0f,
                                         .drive = true,
                                         .handbrake = sz < 0.0f});
  }
  if (wheels.empty()) {
    NJIN_WARN("physics3d: vehicle3d_create: a vehicle needs at least one wheel");
    return vehicle3d_handle{};
  }
  JPH::BodyCreationSettings bs(offset.Get(), JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
                               quat_of(desc.rotation), JPH::EMotionType::Dynamic, layers::moving);
  bs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
  bs.mMassPropertiesOverride.mMass = std::max(desc.mass, 1.0f);
  bs.mFriction = desc.friction;
  const u32 handle = (u32)w.bodies.size() + 1;
  bs.mUserData = handle;
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  JPH::Body *body = bi.CreateBody(bs);
  if (body == nullptr) {
    NJIN_WARN("physics3d: body limit reached");
    return vehicle3d_handle{};
  }
  bi.AddBody(body->GetID(), JPH::EActivation::Activate);
  w.handle_by_body[body->GetID().GetIndexAndSequenceNumber()] = handle;
  w.bodies.push_back(body_slot{.id = body->GetID(), .alive = true, .dynamic = true, .user = desc.user, .vehicle = true});

  JPH::VehicleConstraintSettings vs;
  const f32 travel = std::max(desc.suspension, 0.01f);
  for (const vehicle3d_wheel &wd : wheels) {
    JPH::Ref<JPH::WheelSettingsWV> ws = new JPH::WheelSettingsWV;
    constexpr f32 min_length = 0.05f;
    ws->mSuspensionMinLength = min_length;
    ws->mSuspensionMaxLength = min_length + travel;
    // Jolt places the top of the suspension; the game gives the wheel's centre at full droop.
    ws->mPosition = jv(wd.position) + JPH::Vec3(0.0f, ws->mSuspensionMaxLength, 0.0f);
    ws->mSuspensionSpring.mFrequency = std::max(desc.suspension_frequency, 0.1f);
    ws->mSuspensionSpring.mDamping = clamp(desc.suspension_damping, 0.0f, 1.0f);
    ws->mRadius = std::max(wd.radius, 0.05f);
    ws->mWidth = std::max(wd.width, 0.02f);
    ws->mMaxSteerAngle = wd.steer ? JPH::DegreesToRadians(clamp(desc.max_steer, 0.0f, 89.0f)) : 0.0f;
    ws->mMaxBrakeTorque = std::max(desc.brake_torque, 0.0f);
    ws->mMaxHandBrakeTorque = wd.handbrake ? std::max(desc.handbrake_torque, 0.0f) : 0.0f;
    vs.mWheels.push_back(JPH::Ref<JPH::WheelSettings>(ws.GetPtr()));
  }
  // Axles: wheels at about the same z. Left is +x. Each axle with driven
  // wheels gets a differential sharing the engine evenly; each pair an anti-roll bar.
  JPH::Ref<JPH::WheeledVehicleControllerSettings> cs = new JPH::WheeledVehicleControllerSettings;
  cs->mEngine.mMaxTorque = std::max(desc.engine_torque, 0.0f);
  cs->mEngine.mMaxRPM = std::max(desc.max_rpm, cs->mEngine.mMinRPM + 100.0f);
  std::vector<bool> used(wheels.size(), false);
  for (usize i = 0; i < wheels.size(); i++) {
    if (used[i])
      continue;
    i32 left = -1, right = -1;
    for (usize j = i; j < wheels.size(); j++) {
      if (used[j] || std::fabs(wheels[j].position.z - wheels[i].position.z) > 0.1f)
        continue;
      if (wheels[j].position.x >= 0.0f && left < 0)
        left = (i32)j;
      else if (wheels[j].position.x < 0.0f && right < 0)
        right = (i32)j;
      else
        continue;
      used[j] = true;
    }
    if (left >= 0 && right >= 0) {
      JPH::VehicleAntiRollBar bar;
      bar.mLeftWheel = left;
      bar.mRightWheel = right;
      vs.mAntiRollBars.push_back(bar);
    }
    const bool drive_left = left >= 0 && wheels[(usize)left].drive;
    const bool drive_right = right >= 0 && wheels[(usize)right].drive;
    if (drive_left || drive_right) {
      JPH::VehicleDifferentialSettings d;
      d.mLeftWheel = drive_left ? left : -1;
      d.mRightWheel = drive_right ? right : -1;
      cs->mDifferentials.push_back(d);
    }
  }
  for (JPH::VehicleDifferentialSettings &d : cs->mDifferentials)
    d.mEngineTorqueRatio = 1.0f / (f32)cs->mDifferentials.size();
  vs.mController = cs;
  vehicle_slot v;
  v.alive = true;
  v.body = body3d_handle{handle};
  v.constraint = new JPH::VehicleConstraint(*body, vs);
  // As Jolt's own vehicle sample: its tyres were tuned with 10 times this
  // longitudinal grip. With the plain limit the driven wheels saturate every
  // other step (slip 0.13, then 0.003), and the gearbox, which shifts up only
  // on a step with rising rpm and no slip, never leaves first gear.
  static_cast<JPH::WheeledVehicleController *>(v.constraint->GetController())
      ->SetTireMaxImpulseCallback([](JPH::uint, f32 &longitudinal, f32 &lateral, f32 suspension_impulse,
                                     f32 longitudinal_friction, f32 lateral_friction, f32, f32, f32) {
        longitudinal = 10.0f * longitudinal_friction * suspension_impulse;
        lateral = lateral_friction * suspension_impulse;
      });
  v.tester = new JPH::VehicleCollisionTesterCastCylinder(layers::moving);
  v.constraint->SetVehicleCollisionTester(v.tester);
  w.system.AddConstraint(v.constraint);
  w.system.AddStepListener(v.constraint);
  w.vehicles.push_back(std::move(v));
  return vehicle3d_handle{(u32)w.vehicles.size()};
}

void vehicle3d_destroy(context &ctx, vehicle3d_handle handle) {
  vehicle_slot *v = vehicle_of(ctx, handle);
  if (v == nullptr)
    return;
  physics3d_world &w = *ctx.physics3d.world;
  w.system.RemoveStepListener(v->constraint);
  w.system.RemoveConstraint(v->constraint);
  const body3d_handle body = v->body;
  *v = vehicle_slot{};
  body_slot &b = w.bodies[body.id - 1];
  b.vehicle = false;
  body3d_destroy(ctx, body);
}

void vehicle3d_set_input(context &ctx, vehicle3d_handle handle, f32 throttle, f32 steer, f32 brake, f32 handbrake) {
  vehicle_slot *v = vehicle_of(ctx, handle);
  if (v == nullptr || refuse(std::isfinite(throttle) && std::isfinite(steer) && std::isfinite(brake) &&
                                 std::isfinite(handbrake),
                             "vehicle3d_set_input"))
    return;
  v->throttle = clamp(throttle, -1.0f, 1.0f);
  v->steer = clamp(steer, -1.0f, 1.0f);
  v->brake = clamp(brake, 0.0f, 1.0f);
  v->handbrake = clamp(handbrake, 0.0f, 1.0f);
}

body3d_handle vehicle3d_body(const context &ctx, vehicle3d_handle handle) {
  const vehicle_slot *v = vehicle_of(ctx, handle);
  return v != nullptr ? v->body : body3d_handle{};
}

i32 vehicle3d_wheel_count(const context &ctx, vehicle3d_handle handle) {
  const vehicle_slot *v = vehicle_of(ctx, handle);
  return v != nullptr ? (i32)v->constraint->GetWheels().size() : 0;
}

transform3d vehicle3d_wheel_transform(const context &ctx, vehicle3d_handle handle, i32 wheel) {
  const vehicle_slot *v = vehicle_of(ctx, handle);
  if (v == nullptr || wheel < 0 || wheel >= (i32)v->constraint->GetWheels().size())
    return transform3d{};
  // The wheel's right (its axle) on y, as a cylinder stands; its up on x.
  const JPH::RMat44 m = v->constraint->GetWheelWorldTransform((JPH::uint)wheel, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX());
  return transform3d{.position = world_vec(m.GetTranslation()), .rotation = degrees_of(m.GetQuaternion())};
}

bool vehicle3d_wheel_grounded(const context &ctx, vehicle3d_handle handle, i32 wheel) {
  const vehicle_slot *v = vehicle_of(ctx, handle);
  return v != nullptr && wheel >= 0 && wheel < (i32)v->constraint->GetWheels().size() &&
         v->constraint->GetWheel((JPH::uint)wheel)->HasContact();
}

f32 vehicle3d_rpm(const context &ctx, vehicle3d_handle handle) {
  const vehicle_slot *v = vehicle_of(ctx, handle);
  if (v == nullptr)
    return 0.0f;
  return static_cast<const JPH::WheeledVehicleController *>(v->constraint->GetController())->GetEngine().GetCurrentRPM();
}

i32 vehicle3d_gear(const context &ctx, vehicle3d_handle handle) {
  const vehicle_slot *v = vehicle_of(ctx, handle);
  if (v == nullptr)
    return 0;
  return static_cast<const JPH::WheeledVehicleController *>(v->constraint->GetController())
      ->GetTransmission()
      .GetCurrentGear();
}

void physics3d_set_gravity(context &ctx, vec3 gravity) {
  ctx.physics3d.gravity = gravity;
  if (ctx.physics3d.world)
    ctx.physics3d.world->system.SetGravity(jv(gravity));
}

vec3 physics3d_gravity(const context &ctx) { return ctx.physics3d.gravity; }
} // namespace njin
