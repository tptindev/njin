#include "dna.h"
#include <cstdio>

namespace crowd {
namespace {
// Word 0 is full (24 bits), word 1 holds 10: 14 bits are spare for new genes.
constexpr gene_info table[gene_count] = {
    {"build", 0, 0, 2, 3},        {"hair_style", 0, 2, 3, 6}, {"height", 0, 5, 4, 16},
    {"skin", 0, 9, 4, 16},        {"hair_color", 0, 13, 4, 16}, {"limb", 0, 17, 4, 16},
    {"shoe", 0, 21, 3, 8},        {"cloth_hue", 1, 0, 6, 64},  {"cloth_sat", 1, 6, 2, 4},
    {"cloth_val", 1, 8, 2, 4},
};
} // namespace

const gene_info &gene_desc(gene g) { return table[g]; }

u32 dna::get(gene g) const {
  const gene_info &d = table[g];
  return (word[d.word] >> d.shift) & ((1u << d.bits) - 1u);
}

void dna::set(gene g, u32 value) {
  const gene_info &d = table[g];
  const u32 mask = ((1u << d.bits) - 1u) << d.shift;
  word[d.word] = (word[d.word] & ~mask) | ((value % d.count) << d.shift & mask);
}

dna dna::random(njin::rng &r) {
  dna d;
  for (u32 i = 0; i < gene_count; i++)
    d.set((gene)i, r.next_u32() % table[i].count);
  return d;
}

dna dna::cross(const dna &a, const dna &b, njin::rng &r) {
  dna d;
  for (u32 i = 0; i < gene_count; i++)
    d.set((gene)i, (r.next_u32() & 1u) ? a.get((gene)i) : b.get((gene)i));
  return d;
}

void dna::mutate(njin::rng &r, float rate) {
  for (u32 i = 0; i < gene_count; i++) {
    if (r.unit() >= rate)
      continue;
    const gene g = (gene)i;
    const u32 count = table[i].count;
    if (g == g_cloth_hue || g == g_height) {
      const int step = (r.next_u32() & 1u) ? 1 + (int)(r.next_u32() % 3) : -1 - (int)(r.next_u32() % 3);
      int v = (int)get(g) + step;
      if (g == g_cloth_hue)
        v = (v + (int)count) % (int)count; // hue wraps round
      else
        v = v < 0 ? 0 : (v >= (int)count ? (int)count - 1 : v);
      set(g, (u32)v);
    } else {
      set(g, r.next_u32() % count);
    }
  }
}

std::string dna::hex() const {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%06x-%06x", word[0] & 0xffffffu, word[1] & 0xffffffu);
  return buf;
}
} // namespace crowd
