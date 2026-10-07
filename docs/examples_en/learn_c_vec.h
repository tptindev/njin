#ifndef LEARN_C_VEC_H
#define LEARN_C_VEC_H

typedef struct {
  float x, y;
} vec2;

// Declaration: a promise that these functions exist in another file.
vec2 vec2_add(vec2 a, vec2 b);
float vec2_length(vec2 v);

// extern: this variable is real, but it is defined in learn_c_vec.c.
extern int vec2_calls;

#endif // LEARN_C_VEC_H
