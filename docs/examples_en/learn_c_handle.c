#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Handle: an identifying number, not an address. id 0 is "invalid".
typedef struct {
  uint32_t id;
} sprite_handle;

typedef struct {
  bool alive;
  float x, y;
} sprite_slot;

#define MAX_SPRITES 8
static sprite_slot slots[MAX_SPRITES];
static uint32_t created = 0; // handle number N maps to slots[N - 1]; a slot is never reused

static sprite_handle sprite_create(float x, float y) {
  if (created == MAX_SPRITES)
    return (sprite_handle){0}; // out of room: return an invalid handle, do not crash
  slots[created] = (sprite_slot){.alive = true, .x = x, .y = y};
  created++;
  return (sprite_handle){created};
}

// Only this place turns a handle into a pointer, and it checks every bad case.
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
  printf("after destroying a: sprite_get(a) is %s\n", sprite_get(a) == NULL ? "NULL" : "alive");

  sprite_handle c = sprite_create(50.0f, 60.0f); // does not take over a's slot
  printf("c.id=%u, a is still %s\n", c.id, sprite_get(a) == NULL ? "NULL" : "alive");

  sprite_slot *sb = sprite_get(b);
  printf("b at (%.0f, %.0f)\n", sb->x, sb->y);

  sprite_handle none = {0};
  printf("handle 0: %s\n", sprite_get(none) == NULL ? "NULL, no crash" : "alive");
  sprite_destroy(a); // destroying twice: harmless
  return 0;
}
