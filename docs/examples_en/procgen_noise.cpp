#include <cstdio>
#include <njin.h>

using namespace njin;

// Print a noise field as text: '#' where the noise is above `level`, '.' where it is below.
void show(const char *title, const noise_desc &d, f32 level) {
  std::printf("%s\n", title);
  for (i32 y = 0; y < 14; y++) {
    for (i32 x = 0; x < 60; x++)
      std::putchar(noise_2d(d, (f32)x, (f32)y * 2.0f) > level ? '#' : '.'); // *2: a character is twice as tall as wide
    std::putchar('\n');
  }
}

int main() {
  // Simplest: one layer, just a seed and a frequency. Large, smooth regions.
  show("1. one layer, frequency 0.06", {.seed = 4, .frequency = 0.06f, .fractal = fractal_none}, 0.5f);

  // Add layers (octaves): the same large regions, but the edges get small detail.
  show("2. fBm, 5 layers", {.seed = 4, .frequency = 0.06f, .octaves = 5}, 0.5f);

  // A large gain strengthens the small layers: rougher edges.
  show("3. fBm, 5 layers, gain 0.7", {.seed = 4, .frequency = 0.06f, .octaves = 5, .gain = 0.7f}, 0.5f);

  // Coordinate warp: winding outlines like a real map.
  show("4. fBm, 5 layers, warp 8", {.seed = 4, .frequency = 0.06f, .octaves = 5, .warp = 8.0f}, 0.5f);

  // Sharp ridges: take the high areas for narrow mountain ridges, the low areas for rivers.
  show("5. ridged, 4 layers", {.seed = 4, .frequency = 0.05f, .fractal = fractal_ridged, .octaves = 4}, 0.72f);
}
