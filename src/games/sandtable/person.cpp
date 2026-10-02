#include "person.h"
#include "city/city.h"
#include "view.h"

#include <cmath>

namespace sandtable {

namespace {

// The motions, by the names they have in the file.
struct motion {
  const char *name;
  const char *clip;
  bool loops;
};

const motion motions[] = {
    {"idle", "Idle_Loop", true},        {"talk", "Idle_Talking_Loop", true},
    {"walk", "Walk_Loop", true},        {"jog", "Jog_Fwd_Loop", true},
    {"sprint", "Sprint_Loop", true},    {"jab", "Punch_Jab", false},
    {"cross", "Punch_Cross", false},    {"hit", "Hit_Chest", false},
    {"death", "Death01", false},        {"sit", "Sitting_Idle_Loop", true},
    {"crouch", "Crouch_Idle_Loop", true}, {"aim", "Pistol_Idle_Loop", true},
    {"shoot", "Pistol_Shoot", false},
    // Preserve gameplay action IDs; this source model has no dedicated kick clips.
    {"boxing_guard", "Idle_Loop", true},
    {"boxing_combo", "Punch_Jab", false},
    {"boxing_hook", "Punch_Cross", false},
    {"boxing_front_kick", "Punch_Cross", false},
    {"boxing_round_kick", "Punch_Cross", false},
    {"boxing_block", "Hit_Chest", false},
    {"boxing_low_kick", "Punch_Jab", false},
};
static_assert(sizeof(motions) / sizeof(motions[0]) == static_cast<size_t>(act::count));

constexpr f32 model_height = 1.83f; // the mannequin, metres in the file

model_handle model{};
i32 clip[static_cast<i32>(act::count)]{};
f32 length[static_cast<i32>(act::count)]{};

} // namespace

const char *act_name(act a) { return motions[static_cast<i32>(a)].name; }
bool act_loops(act a) { return motions[static_cast<i32>(a)].loops; }
f32 act_duration(act a) {
  const f32 l = length[static_cast<i32>(a)];
  return l > 0.0f ? l : 1.0f;
}

void person_init(context &ctx) {
  person_cleanup(ctx);
  model = model_load(ctx, "assets/models/person.glb");
  if (model.id == 0) {
    NJIN_WARN("person: assets/models/person.glb did not load");
    return;
  }
  // The file's mannequin is orange with purple joints. White body and grey
  // joints instead, so each man's tint is what he wears.
  for (i32 i = 0; i < model_material_count(ctx, model); ++i) {
    model_material mat = model_material_get(ctx, model, i);
    mat.color = i == 0 ? rgba{0.95f, 0.95f, 0.95f, 1.0f} : rgba{0.42f, 0.4f, 0.38f, 1.0f};
    model_material_set(ctx, model, i, mat);
  }
  for (i32 i = 0; i < static_cast<i32>(act::count); ++i) {
    clip[i] = model_anim_find(ctx, model, motions[i].clip);
    length[i] = clip[i] >= 0 ? model_anim_duration(ctx, model, clip[i]) : 0.0f;
    if (clip[i] < 0)
      NJIN_WARN("person: no animation %s in the model", motions[i].clip);
  }
}

void person_cleanup(context &ctx) {
  if (model.id != 0)
    model_unload(ctx, model);
  model = {};
}

bool person_ready() { return model.id != 0; }

void draw_person(context &ctx, const person_draw &p) {
  if (model.id == 0)
    return;
  const f32 scale = city::person_height * unit3d / model_height;
  // The mannequin faces +z: turn that to the table direction.
  const vec2 d = from_angle(p.facing);
  const f32 yaw = std::atan2(d.x, d.y) * 180.0f / pi;
  const i32 now = static_cast<i32>(p.now), was = static_cast<i32>(p.was);
  model_pose pose{.anim = clip[now], .time = p.time, .loop = motions[now].loops};
  if (p.blend > 0.0f && p.was != p.now) {
    pose.blend_anim = clip[was];
    pose.blend_time = p.was_time;
    pose.blend_loop = motions[was].loops;
    pose.blend = p.blend;
  }
  const f32 width = clamp(p.style.width, .8f, 1.3f);
  draw_model_anim(ctx, model,
                  {.position = to3d(p.at, p.lift), .rotation = {0.0f, yaw, 0.0f}, .scale = {scale * width, scale, scale * width}},
                  pose, p.tint);
}

} // namespace sandtable
