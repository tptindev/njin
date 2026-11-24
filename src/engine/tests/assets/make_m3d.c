/* Writes bend.m3d for the model_formats test, with the exporter of the vendored m3d.h:
 * the model of make_model_formats.py's bend.iqm (a quad on a root bone and its child one
 * unit up) and one action "bend" of three frames (rest, the child turned 90 degrees
 * about Z, the same again).
 *
 *   gcc -O1 -I third_party/m3d src/engine/tests/assets/make_m3d.c -o make_m3d -lm
 *   ./make_m3d src/engine/tests/assets/bend.m3d
 */
#define M3D_IMPLEMENTATION
#define M3D_EXPORTER
#include <m3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: make_m3d <out.m3d>\n");
    return 2;
  }
  const float half = (float)sqrt(0.5);
  /* 0..3 the quad, 4 the normal, 5 origin, 6 identity rotation, 7 one up, 8 the bend */
  m3dv_t vertex[9];
  memset(vertex, 0, sizeof vertex);
  const float points[9][4] = {{-0.5f, 0, 0, 1}, {0.5f, 0, 0, 1}, {0.5f, 2, 0, 1}, {-0.5f, 2, 0, 1}, {0, 0, 1, 1},
                              {0, 0, 0, 1},     {0, 0, 0, 1},   {0, 1, 0, 1},     {0, 0, half, half}};
  for (int i = 0; i < 9; i++) {
    vertex[i].x = points[i][0];
    vertex[i].y = points[i][1];
    vertex[i].z = points[i][2];
    vertex[i].w = points[i][3];
    vertex[i].color = 0;
    vertex[i].skinid = M3D_UNDEF;
  }
  m3ds_t skin[2];
  memset(skin, 0, sizeof skin);
  for (int s = 0; s < 2; s++) {
    for (int k = 0; k < M3D_NUMBONE; k++) {
      skin[s].boneid[k] = M3D_UNDEF;
      skin[s].weight[k] = 0;
    }
    skin[s].boneid[0] = (M3D_INDEX)s;
    skin[s].weight[0] = 1;
  }
  vertex[0].skinid = vertex[1].skinid = 0;
  vertex[2].skinid = vertex[3].skinid = 1;
  m3df_t face[2];
  memset(face, 0, sizeof face);
  const M3D_INDEX tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
  for (int f = 0; f < 2; f++) {
    face[f].materialid = M3D_UNDEF;
    for (int k = 0; k < 3; k++) {
      face[f].vertex[k] = tris[f][k];
      face[f].normal[k] = 4;
      face[f].texcoord[k] = M3D_UNDEF;
    }
  }
  m3db_t bone[2];
  memset(bone, 0, sizeof bone);
  bone[0].parent = M3D_UNDEF;
  bone[0].name = (char *)"root";
  bone[0].pos = 5;
  bone[0].ori = 6;
  bone[1].parent = 0;
  bone[1].name = (char *)"child";
  bone[1].pos = 7;
  bone[1].ori = 6;
  m3dtr_t rest[2] = {{0, 5, 6}, {1, 7, 6}};
  m3dtr_t bent[2] = {{0, 5, 6}, {1, 7, 8}};
  m3dfr_t frame[3] = {{0, 2, rest}, {33, 2, bent}, {66, 2, bent}};
  m3da_t action = {(char *)"bend", 100, 3, frame};

  m3d_t model;
  memset(&model, 0, sizeof model);
  model.name = (char *)"bend";
  model.license = (char *)"MIT";
  model.author = (char *)"njin";
  model.scale = 1;
  model.numvertex = 9;
  model.vertex = vertex;
  model.numskin = 2;
  model.skin = skin;
  model.numface = 2;
  model.face = face;
  model.numbone = 2;
  model.bone = bone;
  model.numaction = 1;
  model.action = &action;
  unsigned int size = 0;
  unsigned char *data = m3d_save(&model, M3D_EXP_FLOAT, M3D_EXP_NOZLIB | M3D_EXP_NORECALC, &size);
  if (!data || !size) {
    fprintf(stderr, "make_m3d: m3d_save failed\n");
    return 1;
  }
  FILE *out = fopen(argv[1], "wb");
  if (!out || fwrite(data, 1, size, out) != size) {
    fprintf(stderr, "make_m3d: cannot write %s\n", argv[1]);
    return 1;
  }
  fclose(out);
  printf("wrote %s (%u bytes)\n", argv[1], size);
  return 0;
}
