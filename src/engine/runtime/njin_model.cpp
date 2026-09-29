#include "njin_model.h"
#include "njin_log.h"
#include "njin_path.h"
#include <rlgl.h>
#include <string>
#include <unordered_set>

namespace njin {
namespace {
// raylib allocates this many maps per material (MAX_MATERIAL_MAPS in its
// config.h, not exported by raylib.h).
constexpr i32 material_map_count = 12;
} // namespace

model_handle model_store_load(model_store &store, const char *path) {
  if (path == nullptr) {
    NJIN_WARN("model: path is null");
    return model_handle{};
  }
  // raylib only logs a missing file; fail loudly with the path instead.
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("model: file not found: %s", path);
    return model_handle{};
  }
  const Model model = LoadModel(resolved.c_str());
  if (!IsModelValid(model)) {
    NJIN_WARN("model: failed to load: %s", path);
    return model_handle{};
  }
  model_slot slot{.model = model, .alive = true, .materials = {}, .bounds = GetModelBoundingBox(model)};
  for (i32 i = 0; i < model.materialCount; i++) {
    const Color c = model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color;
    const Color e = model.materials[i].maps[MATERIAL_MAP_EMISSION].color;
    slot.materials.push_back(model_material{
        .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
        .emission_color = {e.r / 255.0f, e.g / 255.0f, e.b / 255.0f, 1.0f}});
  }
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

void model_store_unload(model_store &store, model_handle handle) {
  model_slot *slot = model_slot_of(store, handle);
  if (slot == nullptr)
    return;
  // UnloadModel frees the meshes and the map arrays but leaves the material
  // textures to the caller. They are not shared outside the model; within it,
  // two maps may name the same texture, so free each id once.
  std::unordered_set<u32> freed;
  for (i32 i = 0; i < slot->model.materialCount; i++) {
    const Material &material = slot->model.materials[i];
    for (i32 m = 0; m < material_map_count; m++) {
      const Texture2D &texture = material.maps[m].texture;
      if (texture.id != 0 && texture.id != rlGetTextureIdDefault() &&
          freed.insert(texture.id).second)
        UnloadTexture(texture);
    }
  }
  UnloadModel(slot->model);
  *slot = model_slot{};
}

model_store::~model_store() {
  for (usize i = 0; i < slots.size(); i++)
    model_store_unload(*this, model_handle{.id = (u32)(i + 1)});
}
} // namespace njin
