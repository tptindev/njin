#include <stdio.h>

typedef struct {
  float run_speed;
  float jump_speed;
  float gravity;
} body_config;

// Default values: every field is named, no need to remember the order.
#define BODY_DEFAULT {.run_speed = 110.0f, .jump_speed = 300.0f, .gravity = 1000.0f}

// Pass by value: the function gets a COPY. Changing the copy does not affect the caller.
static void boost_copy(body_config body) { body.jump_speed += 100.0f; }

// Pass a pointer: the function changes the original. "->" is short for (*body).jump_speed.
static void boost(body_config *body) { body->jump_speed += 100.0f; }

// "const body_config *": look, do not touch. The whole struct is not copied.
static float jump_height(const body_config *body) {
  return body->jump_speed * body->jump_speed / (2.0f * body->gravity);
}

int main(void) {
  body_config body = BODY_DEFAULT;
  body.jump_speed = 350.0f; // change one field, keep the others

  boost_copy(body);
  printf("after boost_copy: jump_speed=%.0f\n", body.jump_speed);

  boost(&body);
  printf("after boost:      jump_speed=%.0f\n", body.jump_speed);

  printf("jump height=%.1f, sizeof(body_config)=%zu bytes\n", jump_height(&body), sizeof(body_config));
  return 0;
}
