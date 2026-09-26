#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Handle: một con số định danh, không phải địa chỉ. id 0 là "không hợp lệ".
typedef struct {
  uint32_t id;
} sprite_handle;

typedef struct {
  bool alive;
  float x, y;
} sprite_slot;

#define MAX_SPRITES 8
static sprite_slot slots[MAX_SPRITES];
static uint32_t created = 0; // handle số N ứng với slots[N - 1]; slot không bao giờ được dùng lại

static sprite_handle sprite_create(float x, float y) {
  if (created == MAX_SPRITES)
    return (sprite_handle){0}; // hết chỗ: trả handle không hợp lệ, không sập
  slots[created] = (sprite_slot){.alive = true, .x = x, .y = y};
  created++;
  return (sprite_handle){created};
}

// Chỉ nơi này biến handle thành con trỏ, và nó kiểm tra mọi trường hợp xấu.
static sprite_slot *sprite_get(sprite_handle h) {
  if (h.id == 0 || h.id > created || !slots[h.id - 1].alive)
    return NULL;
  return &slots[h.id - 1];
}

static void sprite_destroy(sprite_handle h) {
  sprite_slot *s = sprite_get(h);
  if (s != NULL)
    s->alive = false;
}

int main(void) {
  sprite_handle a = sprite_create(10.0f, 20.0f);
  sprite_handle b = sprite_create(30.0f, 40.0f);
  printf("a.id=%u b.id=%u\n", a.id, b.id);

  sprite_destroy(a);
  printf("sau khi huy a: sprite_get(a) la %s\n", sprite_get(a) == NULL ? "NULL" : "con song");

  sprite_handle c = sprite_create(50.0f, 60.0f); // không chiếm lại chỗ của a
  printf("c.id=%u, a van la %s\n", c.id, sprite_get(a) == NULL ? "NULL" : "con song");

  sprite_slot *sb = sprite_get(b);
  printf("b o (%.0f, %.0f)\n", sb->x, sb->y);

  sprite_handle none = {0};
  printf("handle 0: %s\n", sprite_get(none) == NULL ? "NULL, khong sap" : "con song");
  sprite_destroy(a); // hủy lần hai: vô hại
  return 0;
}
