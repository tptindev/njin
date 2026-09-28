#pragma once
#include <njin.h>
#include <string>

namespace crowd {
using njin::u32;
using njin::u8;

// A person's DNA: packed genes in two 24-bit words. 24 bits so each word is an
// exact float in the instance buffer, and the GPU decodes the genes itself
// (assets/shaders/crowd.vs). The table in dna.cpp and the decoder in crowd.vs
// must agree bit for bit.
//
// Two kinds of gene:
// - build chooses a block of the body sheet, so its count multiplies the sheet
//   size: keep it small;
// - everything else is applied per instance while drawing (hair styles live in
//   a small separate head sheet), so it is nearly free: add as many as the
//   spare bits allow.
enum gene : u8 {
  g_build,      // 0 slim, 1 normal, 2 stocky (baked into the body sheet)
  g_hair_style, // 0 short, 1 buzz, 2 long, 3 bun, 4 ponytail, 5 afro (head sheet)
  g_height,     // 0..15: 92% .. 108% tall
  g_skin,       // skin tone palette, 16
  g_hair_color, // hair palette, 16
  g_limb,       // arms and legs (sleeves, trousers), 16
  g_shoe,       // shoes, 8
  g_cloth_hue,  // shirt hue, 64 steps round the colour wheel
  g_cloth_sat,  // shirt saturation, 4 levels
  g_cloth_val,  // shirt brightness, 4 levels
  gene_count,
};

struct gene_info {
  const char *name;
  u8 word;  // 0 or 1
  u8 shift; // first bit in the word
  u8 bits;
  u8 count; // valid values are 0 .. count-1 (count <= 1 << bits)
};

const gene_info &gene_desc(gene g);

inline constexpr u32 builds = 3;
inline constexpr u32 hair_styles = 6;

struct dna {
  u32 word[2] = {0, 0};

  u32 get(gene g) const;
  void set(gene g, u32 value);

  static dna random(njin::rng &r);
  // Each gene from one parent or the other.
  static dna cross(const dna &a, const dna &b, njin::rng &r);
  // Re-rolls each gene with probability `rate`; hue and height drift instead,
  // so a mutated child still looks related.
  void mutate(njin::rng &r, float rate);

  // "a1b2c3-0004d5": the two words in hex.
  std::string hex() const;
};
} // namespace crowd
