#ifndef LEARN_C_VEC_H
#define LEARN_C_VEC_H

typedef struct {
  float x, y;
} vec2;

// Khai báo (declaration): hứa rằng các hàm này tồn tại ở một file khác.
vec2 vec2_add(vec2 a, vec2 b);
float vec2_length(vec2 v);

// extern: biến này có thật, nhưng được định nghĩa trong learn_c_vec.c.
extern int vec2_calls;

#endif // LEARN_C_VEC_H
