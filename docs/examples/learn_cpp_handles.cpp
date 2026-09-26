// Kho handle: slot + id, id 0 là "không hợp lệ", slot không bao giờ dùng lại.
// Mô phỏng cách njin lưu sound, texture, shader (src/engine/runtime/njin_audio_impl.h).
// Biên dịch: g++ -std=c++20 -Wall -Wextra -Wpedantic learn_cpp_handles.cpp -o handles
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct sound_handle {
  unsigned id = 0; // id N nằm ở slots[N - 1]; 0 là không hợp lệ
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
  return {static_cast<unsigned>(store.slots.size())}; // id = vị trí + 1
}

// Trả về slot, hoặc nullptr nếu handle không hợp lệ, sai, hoặc đã unload.
sound_slot *sound_slot_of(sound_store &store, sound_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  sound_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

void sound_unload(sound_store &store, sound_handle handle) {
  if (sound_slot *slot = sound_slot_of(store, handle)) {
    slot->alive = false; // đánh dấu đã chết, KHÔNG xóa khỏi vector và không dùng lại chỗ này
    slot->name.clear();
  }
}

void sound_set_volume(sound_store &store, sound_handle handle, float volume) {
  if (sound_slot *slot = sound_slot_of(store, handle)) // handle xấu thì bỏ qua, không lỗi
    slot->volume = volume;
}

int main() {
  sound_store store;
  const sound_handle hit = sound_load(store, "hit.wav");
  const sound_handle bgm = sound_load(store, "bgm.ogg");
  std::printf("hit.id = %u, bgm.id = %u\n", hit.id, bgm.id);

  sound_unload(store, hit);
  const sound_handle coin = sound_load(store, "coin.wav"); // không chiếm chỗ của hit
  std::printf("coin.id = %u (khong dung lai id cua hit)\n", coin.id);
  std::printf("hit con hop le: %s\n", sound_slot_of(store, hit) ? "yes" : "no");
  std::printf("bgm con hop le: %s\n", sound_slot_of(store, bgm) ? "yes" : "no");

  sound_set_volume(store, hit, 0.5f); // handle cũ: bị bỏ qua, không hỏng gì
  sound_set_volume(store, sound_handle{}, 0.5f); // id 0: bị bỏ qua
  std::printf("coin volume = %.1f\n", sound_slot_of(store, coin)->volume);

  // Con trỏ giữ lâu thì hỏng khi kho lớn lên; handle thì không.
  const std::uintptr_t before = reinterpret_cast<std::uintptr_t>(sound_slot_of(store, bgm));
  for (int i = 0; i < 100; i++)
    sound_load(store, "more.wav"); // vector cấp phát lại
  const std::uintptr_t after = reinterpret_cast<std::uintptr_t>(sound_slot_of(store, bgm));
  std::printf("slot cua bgm da doi cho: %s\n", before != after ? "yes" : "no");
  std::printf("bgm van dung duoc qua handle: %s\n", sound_slot_of(store, bgm) ? "yes" : "no");
  return 0;
}
