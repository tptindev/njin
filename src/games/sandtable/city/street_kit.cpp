#include "street_kit.h"

#include <algorithm>
#include <mutex>

namespace sandtable::city::street {

namespace {

vec3 v3(const json_value &j) { return {j[usize{0}].f32_or(0.0f), j[usize{1}].f32_or(0.0f), j[usize{2}].f32_or(0.0f)}; }

u32 hash(u32 look, u32 salt) {
  u32 h = look ^ (salt * 0x9E3779B9u);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  return h ^ (h >> 16);
}

struct group {
  const char *ids[4];
  f32 weights[4];
};

// distribution_rules.json's groups for the kinds the town places.
const asset *pick(const group &g, u32 look) {
  f32 sum = 0.0f;
  for (const f32 w : g.weights)
    sum += w;
  f32 x = static_cast<f32>(hash(look, 77u) % 10000u) / 10000.0f * sum;
  for (i32 i = 0; i < 4; ++i) {
    if (!g.ids[i])
      break;
    if (x < g.weights[i] || i == 3 || !g.ids[i + 1])
      return find(g.ids[i]);
    x -= g.weights[i];
  }
  return nullptr;
}

} // namespace

const std::vector<asset> &assets() {
  static std::vector<asset> list;
  static std::once_flag once;
  std::call_once(once, [] {
    json_value j;
    const std::string path = std::string(kit_dir) + "/export_manifest.json";
    if (!json_load(path.c_str(), j)) {
      NJIN_WARN("street: %s did not load", path.c_str());
      return;
    }
    if (std::string(j["units"].string_or("")) != "metre" || std::string(j["axes"]["up"].string_or("")) != "+Y") {
      NJIN_WARN("street: %s is not in metres with +Y up", path.c_str());
      return;
    }
    for (usize i = 0; i < j["assets"].size(); ++i) {
      const json_value &e = j["assets"][i];
      asset a;
      a.id = e["id"].string_or("");
      a.path = std::string(kit_dir) + "/" + e["path"].string_or("");
      // The manifest is in Blender's axes (x, y depth, z up); the GLB in
      // glTF's (x, y up, z = -y): turned once, here.
      const vec3 lo = v3(e["bounds_m"]["min"]), hi = v3(e["bounds_m"]["max"]);
      a.size = {hi.x - lo.x, hi.z - lo.z, hi.y - lo.y};
      a.height = hi.z - lo.z;
      const vec3 cc = v3(e["collision"]["center"]), cs = v3(e["collision"]["size"]);
      a.foot_c = {cc.x, -cc.y};
      a.foot_half = {cs.x * 0.5f, cs.y * 0.5f};
      a.post = e.has("light_points") || e.has("wire_sockets");
      if (a.post) {
        // Only the post on the ground (it stands at the origin); the arms
        // and the cross-arm are overhead, out of everyone's way.
        const f32 r = std::min(cs.y, 0.36f) * 0.5f;
        a.foot_c = {};
        a.foot_half = {r, r};
      }
      if (e.has("light_points")) {
        const vec3 l = v3(e["light_points"][usize{0}]);
        a.light = {l.x, l.z, -l.y};
      }
      list.push_back(std::move(a));
    }
  });
  return list;
}

const asset *find(const std::string &id) {
  for (const asset &a : assets())
    if (a.id == id)
      return &a;
  return nullptr;
}

const asset *asset_for(const prop &p) {
  static const group lamps{{"street/StreetLampSingle", "street/StreetLampDouble", nullptr, nullptr}, {4, 1, 0, 0}};
  static const group poles{{"street/UtilityPole", nullptr, nullptr, nullptr}, {1, 0, 0, 0}};
  static const group benches{{"street/StoneBench", "street/StoneBenchBack", "street/PublicBench", "street/PublicBenchBack"},
                             {1, 3, 1, 3}};
  static const group stools{{"street/PlasticStoolLow", "street/PlasticStoolHigh", "street/PlasticChairLow", nullptr},
                            {3, 2, 2, 0}};
  static const group carts{{"street/HuTieuCart", "street/BanhMiCart", "street/MiQuangCounter", nullptr}, {1, 1, 1, 0}};
  switch (p.kind) {
  case prop_kind::lamp: return pick(lamps, p.look);
  case prop_kind::pole: return pick(poles, p.look);
  case prop_kind::bench: return pick(benches, p.look);
  case prop_kind::stool: return pick(stools, p.look);
  case prop_kind::stall: return pick(carts, p.look);
  default: return nullptr;
  }
}

obb footprint(const prop &p, const asset &a) {
  const obb o{p.pos, {}, p.angle};
  const vec2 c = p.pos + (o.axis_x() * a.foot_c.x + o.axis_y() * a.foot_c.y) * units_per_metre;
  return {c, a.foot_half * units_per_metre, p.angle};
}

} // namespace sandtable::city::street
