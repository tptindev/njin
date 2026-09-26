#include <stdio.h>

typedef struct {
  float run_speed;
  float jump_speed;
  float gravity;
} body_config;

// Giá trị mặc định: mỗi trường có tên, không cần nhớ thứ tự.
#define BODY_DEFAULT {.run_speed = 110.0f, .jump_speed = 300.0f, .gravity = 1000.0f}

// Truyền theo giá trị: hàm nhận một BẢN SAO. Sửa bản sao không ảnh hưởng người gọi.
static void boost_copy(body_config body) { body.jump_speed += 100.0f; }

// Truyền con trỏ: hàm sửa bản gốc. "->" là (*body).jump_speed viết gọn.
static void boost(body_config *body) { body->jump_speed += 100.0f; }

// "const body_config *": chỉ nhìn, không sửa. Không sao chép cả struct.
static float jump_height(const body_config *body) {
  return body->jump_speed * body->jump_speed / (2.0f * body->gravity);
}

int main(void) {
  body_config body = BODY_DEFAULT;
  body.jump_speed = 350.0f; // chỉnh một trường, giữ các trường còn lại

  boost_copy(body);
  printf("sau boost_copy: jump_speed=%.0f\n", body.jump_speed);

  boost(&body);
  printf("sau boost:      jump_speed=%.0f\n", body.jump_speed);

  printf("do cao nhay=%.1f, sizeof(body_config)=%zu byte\n", jump_height(&body), sizeof(body_config));
  return 0;
}
