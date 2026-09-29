// 3D physics (njin_physics3d.h) on Jolt Physics (MIT, pulled by the root
// CMakeLists). Two object layers: static bodies, and everything that moves
// (kinematic and dynamic bodies, characters); static bodies never test
// against each other. Characters are Jolt's CharacterVirtual: not bodies of
// the world, moved by velocity with ExtendedUpdate (slides along walls,
// sticks to the floor, walks up steps). The job system is single-threaded:
// a small game's handful of bodies does not pay for threads, and the step
// stays deterministic.
#include "njin_physics3d_impl.h"
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
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
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
  u64 user = 0;
  bool has_target = false;
  vec3 target_pos{};
  vec3 target_rot{};
};

struct character_slot {
  JPH::Ref<JPH::CharacterVirtual> character;
  bool alive = false;
  vec3 desired{};
  f32 step_height = 0.3f;
};

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
} // namespace

struct physics3d_world {
  broad_phase_layers broad_phase;
  object_vs_broad_phase object_vs_broad;
  object_pairs pairs;
  JPH::TempAllocatorImpl temp{10 * 1024 * 1024};
  JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
  JPH::PhysicsSystem system;
  std::vector<body_slot> bodies;         // handle id N is bodies[N - 1]
  std::vector<character_slot> characters; // handle id N is characters[N - 1]

  physics3d_world() {
    system.Init(16384, 0, 16384, 8192, broad_phase, object_vs_broad, pairs);
  }
  ~physics3d_world() {
    characters.clear();
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
// Jolt's globals come before the world and go after it.
physics3d_world &world_of(njin_ctx &ctx) {
  physics3d_state &s = ctx.physics3d;
  if (!s.world) {
    jolt_acquire();
    s.world = std::make_unique<physics3d_world>();
    s.world->system.SetGravity(jv(s.gravity));
  }
  return *s.world;
}

body_slot *body_of(const njin_ctx &ctx, body3d_handle h) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || h.id == 0 || h.id > w->bodies.size())
    return nullptr;
  body_slot &b = w->bodies[h.id - 1];
  return b.alive ? &b : nullptr;
}

character_slot *character_of(const njin_ctx &ctx, character3d_handle h) {
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

void physics3d_step(njin_ctx &ctx, f32 dt) {
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || dt <= 0.0f)
    return;
  JPH::BodyInterface &bi = w->system.GetBodyInterface();
  for (body_slot &b : w->bodies) {
    if (b.alive && b.kinematic && b.has_target) {
      bi.MoveKinematic(b.id, jv(b.target_pos), quat_of(b.target_rot), dt);
      b.has_target = false;
    }
  }
  const JPH::Vec3 gravity = jv(ctx.physics3d.gravity);
  for (character_slot &c : w->characters) {
    if (!c.alive)
      continue;
    c.character->UpdateGroundVelocity();
    c.character->SetLinearVelocity(jv(c.desired));
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, c.step_height, 0.0f);
    c.character->ExtendedUpdate(dt, gravity, settings, w->system.GetDefaultBroadPhaseLayerFilter(layers::moving),
                                w->system.GetDefaultLayerFilter(layers::moving), {}, {}, w->temp);
  }
  w->system.Update(dt, 1, &w->temp, &w->jobs);
}

body3d_handle body3d_create(njin_ctx &ctx, const body3d_desc &desc) {
  // The world first: it sets up Jolt's allocator, which shapes need.
  physics3d_world &w = world_of(ctx);
  JPH::RefConst<JPH::Shape> shape = make_shape(desc);
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
  if (desc.motion == body3d_dynamic) {
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.001f);
  }
  const u32 handle = (u32)w.bodies.size() + 1;
  settings.mUserData = handle;
  JPH::BodyInterface &bi = w.system.GetBodyInterface();
  const JPH::BodyID id = bi.CreateAndAddBody(
      settings, desc.motion == body3d_static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
  if (id.IsInvalid()) {
    NJIN_WARN("physics3d: body limit reached");
    return body3d_handle{};
  }
  w.bodies.push_back(body_slot{.id = id,
                               .alive = true,
                               .kinematic = desc.motion == body3d_kinematic,
                               .user = desc.user,
                               .has_target = false,
                               .target_pos = desc.position,
                               .target_rot = desc.rotation});
  return body3d_handle{handle};
}

void body3d_destroy(njin_ctx &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return;
  JPH::BodyInterface &bi = ctx.physics3d.world->system.GetBodyInterface();
  bi.RemoveBody(b->id);
  bi.DestroyBody(b->id);
  *b = body_slot{};
}

transform3d body3d_transform(const njin_ctx &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return transform3d{};
  const JPH::BodyInterface &bi = ctx.physics3d.world->system.GetBodyInterface();
  JPH::RVec3 p;
  JPH::Quat q;
  bi.GetPositionAndRotation(b->id, p, q);
  return transform3d{.position = {(f32)p.GetX(), (f32)p.GetY(), (f32)p.GetZ()}, .rotation = degrees_of(q)};
}

void body3d_set_position(njin_ctx &ctx, body3d_handle handle, vec3 position, vec3 rotation) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return;
  ctx.physics3d.world->system.GetBodyInterface().SetPositionAndRotation(
      b->id, JPH::RVec3(position.x, position.y, position.z), quat_of(rotation), JPH::EActivation::Activate);
  b->has_target = false;
}

void body3d_move_kinematic(njin_ctx &ctx, body3d_handle handle, vec3 position, vec3 rotation) {
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

vec3 body3d_velocity(const njin_ctx &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  if (b == nullptr)
    return vec3{};
  return nv(ctx.physics3d.world->system.GetBodyInterface().GetLinearVelocity(b->id));
}

void body3d_set_velocity(njin_ctx &ctx, body3d_handle handle, vec3 velocity) {
  body_slot *b = body_of(ctx, handle);
  if (b != nullptr)
    ctx.physics3d.world->system.GetBodyInterface().SetLinearVelocity(b->id, jv(velocity));
}

void body3d_add_impulse(njin_ctx &ctx, body3d_handle handle, vec3 impulse) {
  body_slot *b = body_of(ctx, handle);
  if (b != nullptr)
    ctx.physics3d.world->system.GetBodyInterface().AddImpulse(b->id, jv(impulse));
}

u64 body3d_user(const njin_ctx &ctx, body3d_handle handle) {
  body_slot *b = body_of(ctx, handle);
  return b != nullptr ? b->user : 0;
}

character3d_handle character3d_create(njin_ctx &ctx, const character3d_desc &desc) {
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
  w.characters.push_back(std::move(slot));
  return character3d_handle{(u32)w.characters.size()};
}

void character3d_destroy(njin_ctx &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c != nullptr)
    *c = character_slot{};
}

void character3d_set_velocity(njin_ctx &ctx, character3d_handle handle, vec3 velocity) {
  if (character_slot *c = character_of(ctx, handle))
    c->desired = velocity;
}

vec3 character3d_velocity(const njin_ctx &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  return c != nullptr ? nv(c->character->GetLinearVelocity()) : vec3{};
}

vec3 character3d_position(const njin_ctx &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr)
    return vec3{};
  const JPH::RVec3 p = c->character->GetPosition();
  return vec3{(f32)p.GetX(), (f32)p.GetY(), (f32)p.GetZ()};
}

void character3d_set_position(njin_ctx &ctx, character3d_handle handle, vec3 position) {
  if (character_slot *c = character_of(ctx, handle)) {
    c->character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
    c->character->SetLinearVelocity(JPH::Vec3::sZero());
    c->desired = vec3{};
  }
}

bool character3d_grounded(const njin_ctx &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  return c != nullptr && c->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

vec3 character3d_ground_velocity(const njin_ctx &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr || c->character->GetGroundState() == JPH::CharacterBase::EGroundState::InAir)
    return vec3{};
  return nv(c->character->GetGroundVelocity());
}

body3d_handle character3d_ground_body(const njin_ctx &ctx, character3d_handle handle) {
  character_slot *c = character_of(ctx, handle);
  if (c == nullptr || c->character->GetGroundState() == JPH::CharacterBase::EGroundState::InAir)
    return body3d_handle{};
  return handle_of(*ctx.physics3d.world, c->character->GetGroundBodyID());
}

ray3d_hit physics3d_raycast(const njin_ctx &ctx, const ray3d &ray, f32 max_distance, body3d_handle *body) {
  if (body != nullptr)
    *body = body3d_handle{};
  physics3d_world *w = ctx.physics3d.world.get();
  if (w == nullptr || max_distance <= 0.0f)
    return ray3d_hit{};
  const vec3 dir = normalize(ray.direction);
  const JPH::RRayCast cast{JPH::RVec3(ray.origin.x, ray.origin.y, ray.origin.z), jv(dir * max_distance)};
  JPH::RayCastResult result;
  if (!w->system.GetNarrowPhaseQuery().CastRay(cast, result))
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

void physics3d_set_gravity(njin_ctx &ctx, vec3 gravity) {
  ctx.physics3d.gravity = gravity;
  if (ctx.physics3d.world)
    ctx.physics3d.world->system.SetGravity(jv(gravity));
}

vec3 physics3d_gravity(const njin_ctx &ctx) { return ctx.physics3d.gravity; }
} // namespace njin
