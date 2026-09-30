#include "gang.h"
#include "city/interior.h"
#include "physics.h"
#include "view.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

namespace {

std::vector<gang_state> G;
std::vector<shop_state> S;
std::vector<i8> block_owner;
u32 block_version = 0;
rng gang_rng;

// Each gang's colour: the player's red, then the rivals'.
constexpr rgba colours[] = {rgb(196, 48, 40), rgb(46, 96, 196), rgb(52, 150, 84)};
constexpr i32 gang_count = 3;

// World units a second: the walk clip's stride (as crowd.cpp), a little
// brisker, as they are about business.
constexpr f32 walk_pace = 0.8f * city::person_height;
constexpr f32 turn_rate = 360.0f;
constexpr f32 blend_time = 0.25f;
constexpr f32 talk_seconds = 3.0f; // game seconds at the door (speed 1)
constexpr f32 arrive = 5.0f;       // world units from a door or a place
constexpr f32 stuck_time = 1.5f;

const char *const given[] = {"Tuấn", "Hùng", "Long", "Hải", "Dũng", "Phong", "Khoa", "Nam", "Sơn", "Tâm",
                             "Thắng", "Vũ", "Bình", "Cường", "Đạt", "Lực", "Quang", "Toàn", "Tài", "Hiếu"};
const char *const nick[] = {"Sẹo", "Đen", "Mập", "Còi", "Điên", "Bò", "Lì", "Rổ", "Trọc",
                            "Xăm", "Móm", "Chột", "Lùn", "Cao", "Khều"};

// Where a man stays inside the headquarters: a seat or a place to stand.
struct place {
  vec2 at{};
  f32 facing = 0.0f;
  act pose = act::idle;
};

// Per man (parallel to gang_state::men): his place inside, and a check on
// his walking.
struct man_extra {
  place inside;
  vec2 check_at{};
  f32 check_time = 0.0f;
};
std::vector<std::vector<man_extra>> extras;

const city::building &hq_of(const gang_state &g) { return world().buildings[static_cast<size_t>(g.hq)]; }

void set_act(lackey &m, act a) {
  if (m.now == a)
    return;
  m.was = m.now;
  m.was_time = m.time;
  m.now = a;
  m.time = 0.0f;
  m.blend = 1.0f;
}

void note(gang_state &g, i32 amount, const std::string &what) {
  g.ledger.push_back({state.day, state.hour, amount, what});
  if (g.ledger.size() > 60)
    g.ledger.erase(g.ledger.begin());
}

// --- Inside the headquarters: the higher the rank, the higher the floor --------------

i32 floor_for(rank r, i32 floors) {
  const i32 top = std::max(0, floors - 1);
  switch (r) {
  case rank::boss: return top;
  case rank::deputy: return top;
  case rank::captain: return floors >= 3 ? 1 : top;
  default: return 0;
  }
}

// The seats of a floor (chairs, a place on a sofa) and places to stand, the
// boss's chair first.
std::vector<place> places_on(const city::building &b, i32 floor) {
  const city::interior_layout L = city::build_interior(b, floor);
  std::vector<place> seats, standing;
  for (const city::furn_item &f : L.furniture) {
    const std::string piece = f.piece;
    // A seat faces the way the model's front does (+z of its frame).
    const f32 facing = f.angle + 90.0f;
    if (piece == "chair2") {
      seats.insert(seats.begin(), {f.pos, facing, act::sit});
    } else if (piece == "chair") {
      seats.push_back({f.pos, facing, act::sit});
    } else if (piece == "couchBig" || piece == "couchSmall") {
      const i32 n = piece == "couchBig" ? 3 : 2;
      const f32 w = (piece == "couchBig" ? 2.37f : 1.42f) * city::kit_unit * f.scale;
      for (i32 k = 0; k < n; ++k) {
        const f32 along = (static_cast<f32>(k) + 0.5f) / static_cast<f32>(n) - 0.5f;
        seats.push_back({f.pos + from_angle(f.angle) * (along * w * 0.8f), facing, act::sit});
      }
    }
  }
  // Standing: the middles of the grid's cells clear of the furniture, facing
  // the room's middle.
  const f32 gw = static_cast<f32>(L.nx) * L.cell_x, gd = static_cast<f32>(L.nz) * L.cell_z;
  for (i32 z = 0; z < L.nz; ++z)
    for (i32 x = 0; x < L.nx; ++x) {
      const vec2 c = b.box.center + b.box.axis_x() * ((static_cast<f32>(x) + 0.5f) * L.cell_x - gw * 0.5f) +
                     b.box.axis_y() * ((static_cast<f32>(z) + 0.5f) * L.cell_z - gd * 0.5f);
      bool clear = true;
      for (const city::furn_item &f : L.furniture)
        clear = clear && distance(f.pos, c) > 5.0f;
      for (const vec2 off : {vec2{-3.0f, 0.0f}, vec2{3.0f, 0.0f}})
        if (clear)
          standing.push_back({c + b.box.axis_x() * off.x, angle_of(b.box.center - c) + 0.0f, act::talk});
    }
  seats.insert(seats.end(), standing.begin(), standing.end());
  return seats;
}

// Places inside for every man of gang `gi`, floor by floor by rank.
void seat_men(i32 gi) {
  gang_state &g = G[static_cast<size_t>(gi)];
  const city::building &b = hq_of(g);
  std::vector<std::vector<place>> floors(static_cast<size_t>(std::max(1, b.floors)));
  std::vector<i32> used(floors.size(), 0);
  for (i32 f = 0; f < static_cast<i32>(floors.size()); ++f)
    floors[static_cast<size_t>(f)] = places_on(b, f);
  for (size_t i = 0; i < g.men.size(); ++i) {
    lackey &m = g.men[i];
    m.floor = floor_for(m.rk, b.floors);
    std::vector<place> &here = floors[static_cast<size_t>(m.floor)];
    i32 &u = used[static_cast<size_t>(m.floor)];
    place p;
    if (u < static_cast<i32>(here.size()))
      p = here[static_cast<size_t>(u)];
    else // more men than places: in a loose knot by the door
      p = {b.door - b.front() * 6.0f + b.box.axis_x() * (static_cast<f32>(u % 5) - 2.0f) * 3.0f,
           angle_of(-b.front()), act::idle};
    ++u;
    extras[static_cast<size_t>(gi)][i].inside = p;
  }
}

// --- Outside: rows by rank before the door, facing the boss ----------------------------

place muster_place(const gang_state &g, i32 index) {
  const city::building &b = hq_of(g);
  const vec2 out = b.front(), across = b.box.axis_x();
  const lackey &m = g.men[static_cast<size_t>(index)];
  if (m.rk == rank::boss)
    return {b.door + out * (1.5f * city::units_per_metre), angle_of(out), act::idle};
  // The rows: the right hands first, then the captains, then the soldiers,
  // ten to a row.
  i32 row = 0, col = 0, in_row = 0;
  const auto row_of = [](rank r) { return r == rank::deputy ? 0 : r == rank::captain ? 1 : 2; };
  const i32 mine = row_of(m.rk);
  i32 before = 0, same = 0;
  for (i32 i = 0; i < static_cast<i32>(g.men.size()); ++i) {
    const lackey &o = g.men[static_cast<size_t>(i)];
    if (o.rk == rank::boss)
      continue;
    const i32 r = row_of(o.rk);
    if (r < mine)
      ++before;
    if (r == mine) {
      if (i < index)
        ++col;
      ++same;
    }
  }
  (void)before;
  constexpr i32 per_row = 8;
  row = mine == 2 ? 2 + col / per_row : mine;
  in_row = mine == 2 ? std::min(per_row, same - (col / per_row) * per_row) : same;
  const i32 c = mine == 2 ? col % per_row : col;
  // A metre and a bit between men, a metre and a half between rows, the
  // first row three metres from the boss.
  const f32 spread = 1.2f * city::units_per_metre;
  const vec2 at = b.door + out * ((4.5f + static_cast<f32>(row) * 1.5f) * city::units_per_metre) +
                  across * ((static_cast<f32>(c) - static_cast<f32>(in_row - 1) * 0.5f) * spread);
  return {at, angle_of(-out), act::idle};
}

// --- Men ---------------------------------------------------------------------------------

lackey make_man(rank r) {
  lackey m;
  const i32 a = gang_rng.range(0, static_cast<i32>(std::size(given)) - 1);
  const i32 b = gang_rng.range(0, static_cast<i32>(std::size(nick)) - 1);
  m.name = std::string(given[a]) + " " + nick[b];
  m.rk = r;
  const i32 lift = r == rank::boss ? 3 : r == rank::deputy ? 2 : r == rank::captain ? 1 : 0;
  m.strength = std::min(10, gang_rng.range(3, 7) + lift);
  m.grit = std::min(10, gang_rng.range(3, 7) + lift);
  m.wits = std::min(10, gang_rng.range(3, 7) + lift);
  m.wage = r == rank::boss      ? 0
           : r == rank::deputy  ? 260 + gang_rng.range(0, 40)
           : r == rank::captain ? 160 + gang_rng.range(0, 30)
                                : 60 + 6 * (m.strength + m.wits) + gang_rng.range(0, 20);
  return m;
}

void add_man(context &ctx, i32 gi, rank r) {
  gang_state &g = G[static_cast<size_t>(gi)];
  lackey m = make_man(r);
  m.pos = hq_of(g).door;
  m.body = physics_person(ctx, m.pos);
  character3d_set_active(ctx, m.body, false); // inside
  g.men.push_back(std::move(m));
  extras[static_cast<size_t>(gi)].push_back({});
}

bool path_to(lackey &m, vec2 goal) {
  m.agent.path.clear();
  m.agent.next = 0;
  m.agent.reach = 3.0f;
  return nav_find_path(world().foot, m.pos, goal, m.agent.path, {.max_nodes = 40000}) && !m.agent.path.empty();
}

// Out of the headquarters' door onto the physics.
void step_out(context &ctx, lackey &m, const gang_state &g) {
  if (!m.inside)
    return;
  m.inside = false;
  m.pos = hq_of(g).door;
  character3d_set_position(ctx, m.body, to_phys(m.pos));
  character3d_set_active(ctx, m.body, true);
}

// In through the door, to his place.
void step_in(context &ctx, lackey &m, const man_extra &x) {
  m.inside = true;
  m.agent.path.clear();
  character3d_set_active(ctx, m.body, false);
  m.pos = x.inside.at;
  m.facing = x.inside.facing;
  set_act(m, x.inside.pose);
}

void update_turf() {
  const city::city_map &map = world();
  std::vector<i32> total(map.blocks.size(), 0);
  std::vector<std::array<i32, gang_count>> paying(map.blocks.size(), std::array<i32, gang_count>{});
  for (size_t i = 0; i < map.businesses.size(); ++i) {
    const i32 bk = map.businesses[i].block;
    if (bk < 0)
      continue;
    ++total[static_cast<size_t>(bk)];
    if (S[i].owner >= 0)
      ++paying[static_cast<size_t>(bk)][static_cast<size_t>(S[i].owner)];
  }
  bool changed = false;
  for (gang_state &g : G)
    g.turf = 0;
  for (size_t k = 0; k < map.blocks.size(); ++k) {
    i8 who = -1;
    for (i32 gi = 0; gi < static_cast<i32>(G.size()); ++gi)
      if (total[k] > 0 && paying[k][static_cast<size_t>(gi)] * 2 >= total[k])
        who = static_cast<i8>(gi);
    if (who != block_owner[k])
      changed = true;
    block_owner[k] = who;
    if (who >= 0)
      ++G[static_cast<size_t>(who)].turf;
  }
  if (changed)
    ++block_version;
}

// At the door: a shop that pays this gang pays what it owes; any other is
// squeezed, harder if it pays another gang.
void settle(context &ctx, i32 gi, lackey &m) {
  gang_state &g = G[static_cast<size_t>(gi)];
  const city::business &bz = world().businesses[static_cast<size_t>(m.target)];
  shop_state &s = S[static_cast<size_t>(m.target)];
  char line[220];
  if (s.owner == gi) {
    const f32 knack = 1.0f + 0.03f * static_cast<f32>(m.wits - 5);
    m.carrying = static_cast<i32>(static_cast<f32>(s.owed) * knack);
    s.owed = 0;
    if (m.carrying == 0 && gi == 0) {
      std::snprintf(line, sizeof(line), "%s: %s chưa nợ gì", m.name.c_str(), bz.name.c_str());
      ui_toast(ctx, line);
    }
    return;
  }
  const i32 before = s.owner;
  const f32 odds = clamp(0.35f + 0.07f * static_cast<f32>(m.strength) - 0.12f * static_cast<f32>(bz.tier - 1) -
                             (before >= 0 ? 0.25f : 0.0f),
                         0.05f, 0.95f);
  if (gang_rng.chance(odds)) {
    s.owner = gi;
    s.owed = 0;
    m.carrying = std::max(20, bz.protection / 7);
    if (gi == 0) {
      if (before >= 0)
        std::snprintf(line, sizeof(line), "%s bỏ %s, nộp cho băng mình", bz.name.c_str(),
                      G[static_cast<size_t>(before)].name.c_str());
      else
        std::snprintf(line, sizeof(line), "%s chịu nộp bảo kê cho %s", bz.name.c_str(), m.name.c_str());
      ui_toast(ctx, line, {.kind = ui_toast_success});
    }
    update_turf();
  } else {
    m.carrying = 0;
    if (gi == 0) {
      if (before >= 0)
        std::snprintf(line, sizeof(line), "%s: đã có %s bảo kê, đuổi %s về", bz.name.c_str(),
                      G[static_cast<size_t>(before)].name.c_str(), m.name.c_str());
      else
        std::snprintf(line, sizeof(line), "%s đuổi %s về tay trắng", bz.name.c_str(), m.name.c_str());
      ui_toast(ctx, line, {.kind = ui_toast_warning});
    }
  }
  (void)g;
}

void new_day(context &ctx) {
  const city::city_map &map = world();
  for (size_t i = 0; i < map.businesses.size(); ++i)
    if (S[i].owner >= 0)
      S[i].owed += std::max(1, map.businesses[i].protection / 7);
  gang_state &g = G[0];
  const i32 wages = gang_wages_per_day();
  if (wages > 0) {
    g.money -= wages;
    note(g, -wages, "Trả lương anh em");
  }
  char line[120];
  std::snprintf(line, sizeof(line), "Ngày %d: trả lương %dk", state.day, wages);
  ui_toast(ctx, line, {.kind = g.money < 0 ? ui_toast_error : ui_toast_info});
}

void steer(context &ctx, lackey &m, man_extra &x, f32 dt, f32 speed) {
  vec2 want{};
  if (!m.agent.done())
    want = nav_steer(m.agent, m.pos) * (walk_pace * speed);
  // Held up for long (a crowd, a stall): another way to the same place.
  x.check_time += dt * std::max(speed, 0.001f);
  if (x.check_time >= stuck_time) {
    if (!m.agent.done() && distance(m.pos, x.check_at) < walk_pace * stuck_time * 0.25f && !m.agent.path.empty())
      path_to(m, m.agent.path.back());
    x.check_at = m.pos;
    x.check_time = 0.0f;
  }
  const f32 fall = character3d_grounded(ctx, m.body) ? 0.0f : character3d_velocity(ctx, m.body).y - 9.81f * dt;
  const vec3 v = to_phys(want);
  character3d_set_velocity(ctx, m.body, {v.x, fall, v.z});
}

// One man of gang `gi` for a fixed step.
void step_man(context &ctx, i32 gi, i32 i, f32 dt, f32 speed) {
  gang_state &g = G[static_cast<size_t>(gi)];
  lackey &m = g.men[static_cast<size_t>(i)];
  man_extra &x = extras[static_cast<size_t>(gi)][static_cast<size_t>(i)];
  const city::building &hq = hq_of(g);
  if (m.inside) {
    // Idle inside, at his place; mustered, he comes out.
    if (m.task == job::idle && g.mustered)
      step_out(ctx, m, g);
    else
      return;
  }
  const vec2 was = m.pos;
  m.pos = from_phys(character3d_position(ctx, m.body));
  const vec2 moved = m.pos - was;
  m.speed = length(moved) / std::max(dt, 1e-4f);
  if (length_sq(moved) > 1e-6f) {
    const f32 diff = std::fmod(angle_of(moved) - m.facing + 540.0f, 360.0f) - 180.0f;
    const f32 turn = turn_rate * dt * std::max(speed, 1.0f);
    m.facing += clamp(diff, -turn, turn);
  }
  switch (m.task) {
  case job::idle: {
    if (!g.mustered) {
      // Back in through the door.
      if (distance(m.pos, hq.door) < arrive) {
        step_in(ctx, m, x);
        return;
      }
      if (m.agent.done())
        path_to(m, hq.door);
      set_act(m, act::walk);
    } else {
      const place p = muster_place(g, i);
      if (distance(m.pos, p.at) > 2.0f) {
        if (m.agent.done())
          path_to(m, p.at);
        set_act(m, act::walk);
      } else {
        m.agent.path.clear();
        m.facing = p.facing;
        set_act(m, p.pose);
      }
    }
    break;
  }
  case job::going: {
    const vec2 door = world().businesses[static_cast<size_t>(m.target)].door;
    if (distance(m.pos, door) < arrive || m.agent.done()) {
      m.agent.path.clear();
      m.task = job::talking;
      m.timer = talk_seconds;
      set_act(m, act::talk);
      m.facing = angle_of(door - m.pos);
    }
    break;
  }
  case job::talking:
    m.timer -= dt * speed;
    if (m.timer <= 0.0f) {
      settle(ctx, gi, m);
      path_to(m, hq.door);
      m.task = job::returning;
      set_act(m, act::walk);
      x.check_at = m.pos;
      x.check_time = 0.0f;
    }
    break;
  case job::returning:
    if (distance(m.pos, hq.door) < arrive * 2.0f || m.agent.done()) {
      if (m.carrying > 0) {
        const city::business &bz = world().businesses[static_cast<size_t>(m.target)];
        g.money += m.carrying;
        note(g, m.carrying, "Thu ở " + bz.name);
        if (gi == 0) {
          char line[160];
          std::snprintf(line, sizeof(line), "%s mang về %dk từ %s", m.name.c_str(), m.carrying, bz.name.c_str());
          ui_toast(ctx, line, {.kind = ui_toast_success});
        }
      }
      m.carrying = 0;
      m.target = -1;
      m.task = job::idle;
    }
    break;
  }
  steer(ctx, m, x, dt, speed);
}

// The headquarters: of the places a gang could sit, small blocks (a small
// patch to start from), far from each other; the player's nearest the middle.
std::vector<i32> pick_hqs() {
  const city::city_map &map = world();
  // Blocks with a few shops in them: a gang's first turf.
  std::vector<i32> shops_in(map.blocks.size(), 0);
  for (const city::business &bz : map.businesses)
    if (bz.block >= 0)
      ++shops_in[static_cast<size_t>(bz.block)];
  std::vector<i32> cand;
  for (const i32 h : map.hq_sites) {
    const city::building &b = map.buildings[static_cast<size_t>(h)];
    if (b.door_ok && b.block >= 0 && shops_in[static_cast<size_t>(b.block)] >= 2)
      cand.push_back(h);
  }
  if (cand.size() < static_cast<size_t>(gang_count))
    for (const i32 h : map.hq_sites) {
      const city::building &b = map.buildings[static_cast<size_t>(h)];
      if (b.door_ok && b.block >= 0 && std::find(cand.begin(), cand.end(), h) == cand.end())
        cand.push_back(h);
    }
  if (cand.empty())
    return {};
  const auto block_size = [&](i32 h) {
    return static_cast<i32>(map.blocks[static_cast<size_t>(map.buildings[static_cast<size_t>(h)].block)].buildings.size());
  };
  // The smaller half of the blocks.
  std::vector<i32> sizes;
  for (const i32 h : cand)
    sizes.push_back(block_size(h));
  std::sort(sizes.begin(), sizes.end());
  const i32 small = sizes[sizes.size() / 2];
  std::vector<i32> smalls;
  for (const i32 h : cand)
    if (block_size(h) <= small)
      smalls.push_back(h);
  const vec2 mid{map.desc.width * 0.5f, map.desc.height * 0.5f};
  std::vector<i32> out;
  i32 first = smalls[0];
  for (const i32 h : smalls)
    if (distance(map.buildings[static_cast<size_t>(h)].box.center, mid) <
        distance(map.buildings[static_cast<size_t>(first)].box.center, mid))
      first = h;
  out.push_back(first);
  while (static_cast<i32>(out.size()) < gang_count) {
    i32 best = -1;
    f32 best_d = -1.0f;
    for (const i32 h : cand) {
      if (std::find(out.begin(), out.end(), h) != out.end())
        continue;
      f32 d = 1e30f;
      for (const i32 o : out)
        d = std::min(d, distance(map.buildings[static_cast<size_t>(h)].box.center,
                                 map.buildings[static_cast<size_t>(o)].box.center));
      // Small blocks first: a big one counts as nearer.
      d -= static_cast<f32>(std::max(0, block_size(h) - small)) * 40.0f;
      if (d > best_d) {
        best_d = d;
        best = h;
      }
    }
    if (best < 0)
      break;
    out.push_back(best);
  }
  return out;
}

void start_gang(context &ctx, i32 gi, i32 hq) {
  const city::city_map &map = world();
  gang_state &g = G[static_cast<size_t>(gi)];
  g.hq = hq;
  g.colour = colours[gi];
  const city::building &b = map.buildings[static_cast<size_t>(hq)];
  g.name = "Băng " + map.districts[static_cast<size_t>(b.district)].name;
  g.money = gi == 0 ? 1500 : 3000;
  add_man(ctx, gi, rank::boss);
  add_man(ctx, gi, rank::deputy);
  add_man(ctx, gi, rank::captain);
  for (i32 i = 0; i < 3; ++i)
    add_man(ctx, gi, rank::soldier);
  g.men[0].name = "Đại ca " + g.men[0].name.substr(0, g.men[0].name.find(' '));
  seat_men(gi);
  for (size_t i = 0; i < g.men.size(); ++i)
    step_in(ctx, g.men[i], extras[static_cast<size_t>(gi)][i]);
  // Its first turf: the shops of its own block pay it.
  for (size_t i = 0; i < map.businesses.size(); ++i)
    if (map.businesses[i].block == b.block)
      S[i].owner = gi;
  note(g, g.money, "Vốn ban đầu");
}

} // namespace

std::vector<gang_state> &gangs() { return G; }
gang_state &gang() { return G[0]; }
std::vector<shop_state> &shops() { return S; }
const std::vector<i8> &turf_owner() { return block_owner; }
u32 turf_version() { return block_version; }

const char *rank_name(rank r) {
  switch (r) {
  case rank::boss: return "Đại ca";
  case rank::deputy: return "Cánh tay phải";
  case rank::captain: return "Tổ trưởng";
  default: return "Lính";
  }
}

i32 gang_of_hq(i32 b) {
  for (i32 i = 0; i < static_cast<i32>(G.size()); ++i)
    if (G[static_cast<size_t>(i)].hq == b)
      return i;
  return -1;
}

void gang_start(context &ctx, u32 seed) {
  for (const gang_state &g : G)
    for (const lackey &m : g.men)
      if (m.body.id != 0)
        character3d_destroy(ctx, m.body);
  G.clear();
  extras.clear();
  gang_rng = rng(static_cast<u64>(seed) * 104729u + 7u);
  const city::city_map &map = world();
  S.assign(map.businesses.size(), {});
  block_owner.assign(map.blocks.size(), -1);
  ++block_version;
  const std::vector<i32> hqs = pick_hqs();
  std::vector<const city::building *> hq_buildings;
  for (const i32 h : hqs)
    hq_buildings.push_back(&map.buildings[static_cast<size_t>(h)]);
  city::set_gang_hqs(hq_buildings); // before seating: the seats come from their layout
  G.resize(hqs.size());
  extras.resize(hqs.size());
  for (i32 gi = 0; gi < static_cast<i32>(hqs.size()); ++gi)
    start_gang(ctx, gi, hqs[static_cast<size_t>(gi)]);
  update_turf();
}

bool gang_recruit(context &ctx) {
  gang_state &g = G[0];
  if (g.money < recruit_cost)
    return false;
  g.money -= recruit_cost;
  add_man(ctx, 0, rank::soldier);
  seat_men(0);
  step_in(ctx, g.men.back(), extras[0].back());
  note(g, -recruit_cost, "Tuyển " + g.men.back().name);
  return true;
}

void gang_muster(context &, bool out) {
  if (!G.empty())
    G[0].mustered = out;
}

bool gang_send(context &ctx, i32 mi, i32 b) {
  if (G.empty() || mi < 0 || mi >= static_cast<i32>(G[0].men.size()) || b < 0 ||
      b >= static_cast<i32>(world().businesses.size()))
    return false;
  gang_state &g = G[0];
  lackey &m = g.men[static_cast<size_t>(mi)];
  if (m.task != job::idle || m.rk == rank::boss)
    return false;
  step_out(ctx, m, g);
  if (!path_to(m, world().businesses[static_cast<size_t>(b)].door)) {
    if (!g.mustered)
      step_in(ctx, m, extras[0][static_cast<size_t>(mi)]);
    return false;
  }
  m.target = b;
  m.task = job::going;
  set_act(m, act::walk);
  extras[0][static_cast<size_t>(mi)].check_at = m.pos;
  extras[0][static_cast<size_t>(mi)].check_time = 0.0f;
  return true;
}

void gang_step(context &ctx, f32 dt) {
  if (G.empty())
    return;
  const f32 speed = state.popup_open ? 0.0f : state.speed;
  state.hour += dt * speed / seconds_per_hour;
  if (state.hour >= 24.0f) {
    state.hour -= 24.0f;
    ++state.day;
    new_day(ctx);
  }
  for (i32 gi = 0; gi < static_cast<i32>(G.size()); ++gi)
    for (i32 i = 0; i < static_cast<i32>(G[static_cast<size_t>(gi)].men.size()); ++i)
      step_man(ctx, gi, i, dt, speed);
}

void gang_update(f32 dt) {
  const f32 speed = state.popup_open ? 0.0f : state.speed;
  for (gang_state &g : G)
    for (lackey &m : g.men) {
      const f32 pace = m.now == act::walk ? clamp(m.speed / walk_pace, 0.25f, 3.5f) : std::max(speed, 0.2f);
      m.time += dt * pace;
      m.was_time += dt;
      m.blend = std::max(0.0f, m.blend - dt / blend_time);
    }
}

void gang_draw(context &ctx) {
  const city::city_map &map = world();
  const city::view_options &v = world_view();
  for (const gang_state &g : G) {
    if (g.hq < 0)
      continue;
    // The flag on the headquarters' roof, in the gang's colour.
    const city::building &b = map.buildings[static_cast<size_t>(g.hq)];
    const bool open = std::binary_search(v.cut.begin(), v.cut.end(), g.hq);
    if (!open) {
      const vec3 foot = to3d(b.box.center, (b.height + 2.0f) * unit3d);
      material3d_set(ctx, {.specular = 0.2f});
      draw_cylinder3d(ctx, foot, foot + vec3{0.0f, 26.0f * unit3d, 0.0f}, 0.035f, rgb(60, 58, 54));
      draw_cube3d(ctx, foot + vec3{0.28f, 22.0f * unit3d, 0.0f}, {0.55f, 0.32f, 0.03f}, g.colour);
      material3d_set(ctx, {});
    }
  }
  if (!person_ready() || state.cam_distance > 60.0f)
    return;
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  for (const gang_state &g : G) {
    // The floor open of the headquarters, if it is open: the men of that
    // floor are seen inside.
    i32 open_floor = -1;
    if (std::binary_search(v.cut.begin(), v.cut.end(), g.hq))
      open_floor = g.hq == v.selected || v.around ? v.floor : 0;
    for (const lackey &m : g.men) {
      if (m.inside && m.floor != open_floor)
        continue;
      if (!city::view_sees(m.pos, 20.0f))
        continue;
      // The higher the rank, the darker the clothes.
      const f32 dark = m.rk == rank::boss ? 0.35f : m.rk == rank::deputy ? 0.6f : m.rk == rank::captain ? 0.8f : 1.0f;
      const f32 lift = m.inside ? static_cast<f32>(m.floor) * city::floor_height * unit3d + 0.02f : 0.04f;
      draw_person(ctx, {.at = m.pos,
                        .facing = m.facing,
                        .now = m.now,
                        .time = m.time,
                        .was = m.was,
                        .was_time = m.was_time,
                        .blend = m.blend,
                        .tint = {g.colour.r * dark, g.colour.g * dark, g.colour.b * dark, 1.0f},
                        .lift = lift,
                        .identity = static_cast<u32>(std::hash<std::string>{}(m.name))});
    }
  }
  material3d_set(ctx, {});
}

i32 gang_income_per_day() {
  const city::city_map &map = world();
  i32 sum = 0;
  for (size_t i = 0; i < map.businesses.size() && i < S.size(); ++i)
    if (S[i].owner == 0)
      sum += std::max(1, map.businesses[i].protection / 7);
  return sum;
}

i32 gang_wages_per_day() {
  i32 sum = 0;
  if (!G.empty())
    for (const lackey &m : G[0].men)
      sum += m.wage;
  return sum;
}

const char *job_name(job j) {
  switch (j) {
  case job::idle: return "Ở trụ sở";
  case job::going: return "Đang đi";
  case job::talking: return "Đang nói chuyện";
  case job::returning: return "Đang về";
  }
  return "";
}

const char *gang_target_name(const lackey &m) {
  if (m.target < 0)
    return "";
  return world().businesses[static_cast<size_t>(m.target)].name.c_str();
}

} // namespace sandtable
