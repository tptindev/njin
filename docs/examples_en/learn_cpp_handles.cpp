// A handle store: slots + ids, id 0 is "invalid", a slot is never reused.
// Mimics how njin stores sounds, textures, shaders (src/engine/runtime/njin_audio_impl.h).
// Compile: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_handles.cpp -o handles
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct sound_handle {
  unsigned id = 0; // id N lives at slots[N - 1]; 0 is invalid
};

struct sound_slot {
  bool alive = false;
  std::string name;
  float volume = 1.0f;
};

struct sound_store {
  std::vector<sound_slot> slots;
};

sound_handle sound_load(sound_store &store, const char *name) {
  store.slots.push_back({true, name, 1.0f});
  return {static_cast<unsigned>(store.slots.size())}; // id = position + 1
}

// Returns the slot, or nullptr if the handle is invalid, wrong, or already unloaded.
sound_slot *sound_slot_of(sound_store &store, sound_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  sound_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

void sound_unload(sound_store &store, sound_handle handle) {
  if (sound_slot *slot = sound_slot_of(store, handle)) {
    slot->alive = false; // mark as dead, do NOT remove it from the vector and never reuse this spot
    slot->name.clear();
  }
}

void sound_set_volume(sound_store &store, sound_handle handle, float volume) {
  if (sound_slot *slot = sound_slot_of(store, handle)) // a bad handle is ignored, no error
    slot->volume = volume;
}

int main() {
  sound_store store;
  const sound_handle hit = sound_load(store, "hit.wav");
  const sound_handle bgm = sound_load(store, "bgm.ogg");
  std::printf("hit.id = %u, bgm.id = %u\n", hit.id, bgm.id);

  sound_unload(store, hit);
  const sound_handle coin = sound_load(store, "coin.wav"); // does not take hit's place
  std::printf("coin.id = %u (does not reuse hit's id)\n", coin.id);
  std::printf("hit still valid: %s\n", sound_slot_of(store, hit) ? "yes" : "no");
  std::printf("bgm still valid: %s\n", sound_slot_of(store, bgm) ? "yes" : "no");

  sound_set_volume(store, hit, 0.5f); // old handle: ignored, nothing breaks
  sound_set_volume(store, sound_handle{}, 0.5f); // id 0: ignored
  std::printf("coin volume = %.1f\n", sound_slot_of(store, coin)->volume);

  // A pointer kept for long breaks when the store grows; a handle does not.
  const std::uintptr_t before = reinterpret_cast<std::uintptr_t>(sound_slot_of(store, bgm));
  for (int i = 0; i < 100; i++)
    sound_load(store, "more.wav"); // the vector reallocates
  const std::uintptr_t after = reinterpret_cast<std::uintptr_t>(sound_slot_of(store, bgm));
  std::printf("bgm's slot moved: %s\n", before != after ? "yes" : "no");
  std::printf("bgm still usable through its handle: %s\n", sound_slot_of(store, bgm) ? "yes" : "no");
  return 0;
}
