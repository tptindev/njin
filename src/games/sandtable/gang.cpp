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
i32 focus_gi = -1, focus_mi = -1; // the man looked at (gang_focus_man)

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

// The floor of the headquarters open to look into (its men there are drawn
// inside), or -1 when it is not cut open.
i32 open_floor_of(const gang_state &g) {
  const city::view_options &v = world_view();
  if (!std::binary_search(v.cut.begin(), v.cut.end(), g.hq))
    return -1;
  return g.hq == v.selected || v.around ? v.floor : 0;
}

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
  // the room's middle. Indoor men have no physics (they are not on the
  // table), so nothing else keeps two of them apart: a place is good only
  // when it, itself, clears every piece of furniture and every place already
  // claimed, not just the cell's own middle (a cell can pass that and still
  // put a standing place right against a chair near its edge).
  constexpr f32 clearance = 4.0f; // world units, a bit over a shoulder's width
  const auto clear_of = [&](vec2 p) {
    for (const city::furn_item &f : L.furniture)
      if (distance(f.pos, p) < clearance)
        return false;
    for (const place &s : seats)
      if (distance(s.at, p) < clearance)
        return false;
    for (const place &s : standing)
      if (distance(s.at, p) < clearance)
        return false;
    return true;
  };
  const f32 gw = static_cast<f32>(L.nx) * L.cell_x, gd = static_cast<f32>(L.nz) * L.cell_z;
  for (i32 z = 0; z < L.nz; ++z)
    for (i32 x = 0; x < L.nx; ++x) {
      const vec2 c = b.box.center + b.box.axis_x() * ((static_cast<f32>(x) + 0.5f) * L.cell_x - gw * 0.5f) +
                     b.box.axis_y() * ((static_cast<f32>(z) + 0.5f) * L.cell_z - gd * 0.5f);
      for (const vec2 off : {vec2{-3.0f, 0.0f}, vec2{3.0f, 0.0f}}) {
        const vec2 p = c + b.box.axis_x() * off.x;
        if (clear_of(p))
          standing.push_back({p, angle_of(b.box.center - c) + 0.0f, act::talk});
      }
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

// The men line up before the door, on whatever ground is there (sidewalk or
// street): the boss by it, facing out; the others in rows facing him, by
// rank (right hands, captains, soldiers) front to back, eight to a row.
place muster_place(const gang_state &g, i32 index) {
  const city::building &b = hq_of(g);
  const vec2 out = b.front(), across = b.box.axis_x();
  const f32 metre = city::units_per_metre;
  const f32 boss_at = 1.0f * metre;
  const lackey &m = g.men[static_cast<size_t>(index)];
  if (m.rk == rank::boss)
    return {b.door + out * boss_at, angle_of(out), act::idle};
  // His place in the line-up: the others ranked, highest first.
  const auto rank_order = [](rank r) { return r == rank::deputy ? 0 : r == rank::captain ? 1 : 2; };
  i32 count = 0, order = 0;
  for (i32 i = 0; i < static_cast<i32>(g.men.size()); ++i) {
    const lackey &o = g.men[static_cast<size_t>(i)];
    if (o.rk == rank::boss)
      continue;
    ++count;
    if (rank_order(o.rk) < rank_order(m.rk) || (rank_order(o.rk) == rank_order(m.rk) && i < index))
      ++order;
  }
  // Rows a metre apart from 1.5 m past the boss; men a metre apart along a
  // row.
  const f32 first = boss_at + 1.5f * metre;
  const i32 per_row = std::min(8, std::max(1, count));
  const i32 row = order / per_row, col = order % per_row;
  const i32 in_row = std::min(per_row, count - row * per_row);
  const vec2 at = b.door + out * (first + static_cast<f32>(row) * metre) +
                  across * ((static_cast<f32>(col) - static_cast<f32>(in_row - 1) * 0.5f) * metre);
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
  m.wallet = r == rank::boss ? 0 : gang_rng.range(100, 300);
  m.morale = r == rank::boss ? 100.0f : static_cast<f32>(gang_rng.range(62, 80));
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

// A beating: health down, and he is shaken.
void hurt(lackey &m, f32 lo, f32 hi) {
  m.health = std::max(5.0f, m.health - gang_rng.range(lo, hi));
  m.morale = std::max(0.0f, m.morale - 3.0f);
}

// At the door: a shop that pays this gang pays what it owes; any other is
// squeezed, harder if it pays another gang.
void settle(context &ctx, i32 gi, lackey &m) {
  gang_state &g = G[static_cast<size_t>(gi)];
  const city::business &bz = world().businesses[static_cast<size_t>(m.target)];
  shop_state &s = S[static_cast<size_t>(m.target)];
  char line[220];
  if (m.sent_on == errand::raid)
    g.raid.hit.push_back(m.target);
  if (!business_open(m.target)) {
    if (m.sent_on == errand::raid)
      NJIN_INFO("[gang] %s: %s finds %s shut", g.name.c_str(), m.name.c_str(), bz.name.c_str());
    if (gi == 0) {
      std::snprintf(line, sizeof(line), "%s đã đóng cửa, %s về tay không", bz.name.c_str(), m.name.c_str());
      ui_toast(ctx, line, {.kind = ui_toast_warning});
    }
    return;
  }
  if (m.sent_on == errand::raid) {
    // Someone else's shop, paying up on the spot, unless its gang's men are
    // about: then the raid is off.
    if (s.owner < 0 || s.owner == gi)
      return;
    if (gang_guards(s.owner, m.target)) {
      g.raid.on = false;
      if (gang_rng.chance(0.6f))
        hurt(m, 15.0f, 40.0f);
      NJIN_INFO("[gang] %s: %s finds %s's men at %s, raid off", g.name.c_str(), m.name.c_str(),
                G[static_cast<size_t>(s.owner)].name.c_str(), bz.name.c_str());
      if (s.owner == 0) {
        std::snprintf(line, sizeof(line), "Người của %s tới %s, thấy anh em mình nên rút", g.name.c_str(),
                      bz.name.c_str());
        ui_toast(ctx, line, {.kind = ui_toast_success});
      }
      return;
    }
    // About a day of its protection, more for a man who leans harder.
    const f32 knack = 1.0f + 0.05f * static_cast<f32>(m.strength + m.wits - 10);
    const i32 take = std::max(20, static_cast<i32>(static_cast<f32>(bz.protection / 7) * knack));
    m.carrying += take;
    g.raid.taken += take;
    // It has paid once today: what it owes its own gang is that much less.
    s.owed = std::max(0, s.owed - take);
    s.raided_by = gi;
    s.raided_day = state.day;
    s.raided_amount = take;
    NJIN_INFO("[gang] %s: %s takes %dk at %s (%d/%dk)", g.name.c_str(), m.name.c_str(), take, bz.name.c_str(),
              g.raid.taken, g.raid.quota);
    if (g.raid.taken >= g.raid.quota)
      g.raid.on = false;
    return;
  }
  if (s.owner == gi) {
    const f32 knack = 1.0f + 0.03f * static_cast<f32>(m.wits - 5);
    m.carrying = static_cast<i32>(static_cast<f32>(s.owed) * knack);
    s.owed = 0;
    s.last_seen = state.day;
    if (m.carrying == 0 && gi == 0) {
      std::snprintf(line, sizeof(line), "%s: %s chưa nợ gì", m.name.c_str(), bz.name.c_str());
      ui_toast(ctx, line);
    }
    return;
  }
  const i32 before = s.owner;
  const f32 odds = clamp(0.35f + 0.07f * static_cast<f32>(m.strength) - 0.12f * static_cast<f32>(bz.tier - 1) -
                             (before >= 0 ? 0.25f : 0.0f),
                         0.05f, 0.95f) -
                   (m.fatigue > 60.0f ? 0.1f : 0.0f) - (m.health < 70.0f ? 0.1f : 0.0f);
  if (gang_rng.chance(odds)) {
    s.owner = gi;
    s.owed = 0;
    s.last_seen = state.day;
    m.morale = std::min(100.0f, m.morale + 2.0f);
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
    // Thrown out, maybe roughly: harder where another gang's men stand by.
    const bool guarded = before >= 0 && gang_guards(before, m.target);
    const bool beaten = gang_rng.chance(guarded ? 0.7f : 0.2f + 0.05f * static_cast<f32>(bz.tier));
    if (beaten)
      hurt(m, guarded ? 15.0f : 8.0f, guarded ? 40.0f : 25.0f);
    if (gi == 0 && beaten) {
      std::snprintf(line, sizeof(line), "%s bị đánh ở %s, về với vết thương", m.name.c_str(), bz.name.c_str());
      ui_toast(ctx, line, {.kind = ui_toast_error});
    } else if (gi == 0) {
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

// The next shop of a raid for man `m`: one of the raided gang's, open, in the
// raid's block, not called at yet nor someone else's next; the nearest. -1
// when the raid is over.
i32 raid_next(i32 gi, const lackey &m) {
  const gang_state &g = G[static_cast<size_t>(gi)];
  if (!g.raid.on || g.raid.block < 0)
    return -1;
  const city::block &bk = world().blocks[static_cast<size_t>(g.raid.block)];
  i32 best = -1;
  f32 best_d = 1e30f;
  for (const i32 b : bk.businesses) {
    if (S[static_cast<size_t>(b)].owner != g.raid.victim || !business_open(b) ||
        std::find(g.raid.hit.begin(), g.raid.hit.end(), b) != g.raid.hit.end())
      continue;
    bool taken = false;
    for (const lackey &o : g.men)
      if (&o != &m && o.target == b && o.task != job::idle)
        taken = true;
    const f32 d = distance(m.pos, world().businesses[static_cast<size_t>(b)].door);
    if (!taken && d < best_d) {
      best_d = d;
      best = b;
    }
  }
  return best;
}

// The day's upkeep of gang `gi`: wages (what is owed first, the highest ranks
// first), the clinic, each man's food and rent, and how each feels about it.
// Returns the toast line for the player's gang.
std::string upkeep(i32 gi) {
  gang_state &g = G[static_cast<size_t>(gi)];
  std::vector<lackey *> order;
  for (lackey &m : g.men)
    if (m.rk != rank::boss)
      order.push_back(&m);
  std::stable_sort(order.begin(), order.end(), [](const lackey *a, const lackey *b) { return a->rk > b->rk; });
  i32 paid = 0, short_of = 0;
  for (lackey *m : order) {
    const i32 due = m->wage + m->unpaid;
    const i32 give = std::min(due, std::max(0, g.money));
    g.money -= give;
    paid += give;
    m->wallet += give;
    m->unpaid = due - give;
    // Paid today's wage in full (old debts may still be outstanding)?
    m->unpaid_days = m->unpaid > 0 ? m->unpaid_days + 1 : 0;
    short_of += m->unpaid;
  }
  if (paid > 0)
    note(g, -paid, "Trả lương anh em");
  i32 clinic = 0;
  for (lackey &m : g.men) {
    m.treated = false;
    if (m.health < 100.0f && m.treat && g.money >= clinic_cost) {
      g.money -= clinic_cost;
      clinic += clinic_cost;
      m.treated = true;
    }
  }
  if (clinic > 0)
    note(g, -clinic, "Tiền thuốc men");
  i32 unhappy = 0;
  for (lackey &m : g.men) {
    if (m.rk == rank::boss) {
      m.morale = 100.0f;
      continue;
    }
    const i32 cost = living_cost(m.rk);
    m.hungry = m.wallet < cost;
    m.wallet = std::max(0, m.wallet - cost);
    f32 up = 0.0f, down = 0.0f;
    if (m.unpaid_days == 0) {
      up += 3.0f + std::min(3.0f, static_cast<f32>(m.wage - cost) / 30.0f);
    } else {
      down += 8.0f + 6.0f * static_cast<f32>(m.unpaid_days - 1);
    }
    if (m.hungry)
      down += 12.0f;
    if (m.health < 70.0f && !m.treated)
      down += 6.0f;
    if (m.fatigue > 70.0f)
      down += 5.0f;
    if (m.wallet >= cost * 5)
      up += 2.0f;
    // Grit: a hard man takes it better (5 is the usual).
    down *= clamp(1.4f - 0.08f * static_cast<f32>(m.grit), 0.5f, 1.3f);
    m.morale = clamp(m.morale + up - down, 0.0f, 100.0f);
    if (m.morale < quit_morale) {
      ++unhappy;
      const f32 odds = (0.15f + 0.6f * (quit_morale - m.morale) / quit_morale) *
                       clamp(1.2f - 0.05f * static_cast<f32>(m.grit), 0.6f, 1.2f);
      if (gang_rng.chance(odds))
        m.quitting = true;
    }
  }
  char line[200];
  std::snprintf(line, sizeof(line), "Ngày %d: trả lương %dk%s", state.day, paid, clinic > 0 ? ", thuốc men" : "");
  std::string out = line;
  if (short_of > 0) {
    std::snprintf(line, sizeof(line), " · còn nợ lương %dk", short_of);
    out += line;
  }
  if (unhappy > 0) {
    std::snprintf(line, sizeof(line), " · %d người bất mãn", unhappy);
    out += line;
  }
  return out;
}

// Shops their gang has not called at for days, with none of its men about,
// may stop paying.
void neglect(context &ctx) {
  const city::city_map &map = world();
  i32 lost = 0;
  for (size_t i = 0; i < map.businesses.size(); ++i) {
    shop_state &s = S[i];
    if (s.owner < 0)
      continue;
    if (gang_guards(s.owner, static_cast<i32>(i))) {
      s.last_seen = state.day;
      continue;
    }
    const i32 away = state.day - s.last_seen;
    if (away < neglect_days || !gang_rng.chance(std::min(0.8f, 0.25f * static_cast<f32>(away - neglect_days + 1))))
      continue;
    lost += s.owner == 0 ? 1 : 0;
    s.owner = -1;
    s.owed = 0;
  }
  if (lost > 0) {
    char line[160];
    std::snprintf(line, sizeof(line), "%d cơ sở thôi nộp: lâu rồi không ai của mình ghé", lost);
    ui_toast(ctx, line, {.kind = ui_toast_warning, .seconds = 8.0f});
  }
  update_turf();
}

void new_day(context &ctx) {
  const city::city_map &map = world();
  for (size_t i = 0; i < map.businesses.size(); ++i)
    if (S[i].owner >= 0)
      S[i].owed += std::max(1, map.businesses[i].protection / 7);
  std::string mine;
  for (i32 gi = 0; gi < static_cast<i32>(G.size()); ++gi) {
    const std::string line = upkeep(gi);
    if (gi == 0)
      mine = line;
  }
  neglect(ctx);
  const gang_state &g = G[0];
  const bool trouble = gang_wages_owed() > 0;
  ui_toast(ctx, mine.c_str(), {.kind = trouble ? ui_toast_error : ui_toast_info});
  (void)g;
}

// Through the hours: tired out on a job, rested at the headquarters (better
// at night), healing, fast at the clinic.
void wear(f32 hours) {
  const bool night = !hour_between(state.hour, 7.0f, 22.0f);
  for (gang_state &g : G)
    for (lackey &m : g.men) {
      if (m.inside)
        m.fatigue -= (night ? 18.0f : 10.0f) * hours;
      else if (m.task != job::idle)
        m.fatigue += 7.0f * hours;
      else
        m.fatigue += 2.0f * hours;
      m.fatigue = clamp(m.fatigue, 0.0f, 100.0f);
      if (m.health < 100.0f)
        m.health = std::min(100.0f, m.health + (m.treated ? 3.0f : 0.6f) * hours);
    }
}

// Men who have walked out, once they are back in: gone from the gang.
void drop_quitters(context &ctx) {
  for (i32 gi = 0; gi < static_cast<i32>(G.size()); ++gi) {
    gang_state &g = G[static_cast<size_t>(gi)];
    for (i32 i = static_cast<i32>(g.men.size()) - 1; i >= 0; --i) {
      lackey &m = g.men[static_cast<size_t>(i)];
      if (!m.quitting || !m.inside || m.task != job::idle)
        continue;
      if (gi == 0) {
        char line[200];
        std::snprintf(line, sizeof(line), "%s bỏ băng ra đi%s", m.name.c_str(),
                      m.unpaid > 0 ? ": bị nợ lương" : m.hungry ? ": không đủ ăn" : ": chán nản");
        ui_toast(ctx, line, {.kind = ui_toast_error, .seconds = 8.0f});
      }
      NJIN_INFO("[gang] %s: %s walks out (morale %.0f, owed %dk)", g.name.c_str(), m.name.c_str(),
                static_cast<f64>(m.morale), m.unpaid);
      if (m.body.id != 0)
        character3d_destroy(ctx, m.body);
      g.men.erase(g.men.begin() + i);
      extras[static_cast<size_t>(gi)].erase(extras[static_cast<size_t>(gi)].begin() + i);
      if (focus_gi == gi && focus_mi == i)
        focus_gi = focus_mi = -1;
      else if (focus_gi == gi && focus_mi > i)
        --focus_mi;
    }
  }
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
      // On a raid, on to the next shop while there is one.
      if (m.sent_on == errand::raid) {
        const i32 next = raid_next(gi, m);
        if (next >= 0 && path_to(m, world().businesses[static_cast<size_t>(next)].door)) {
          m.target = next;
          m.task = job::going;
          set_act(m, act::walk);
          x.check_at = m.pos;
          x.check_time = 0.0f;
          break;
        }
        g.raid.on = false;
      }
      path_to(m, hq.door);
      m.task = job::returning;
      set_act(m, act::walk);
      x.check_at = m.pos;
      x.check_time = 0.0f;
    }
    break;
  case job::patrolling: {
    // Worn out: home. At each shop: its gang's shops round him are seen to,
    // then on to another.
    if (m.fatigue >= tired_limit || m.beat < 0) {
      path_to(m, hq.door);
      m.task = job::returning;
      break;
    }
    if (m.agent.done() || (m.target >= 0 && distance(m.pos, world().businesses[static_cast<size_t>(m.target)].door) < arrive * 2.0f)) {
      constexpr f32 reach = 12.0f * city::units_per_metre;
      const city::block &bk = world().blocks[static_cast<size_t>(m.beat)];
      for (const i32 b : bk.businesses)
        if (S[static_cast<size_t>(b)].owner == gi && distance(world().businesses[static_cast<size_t>(b)].door, m.pos) < reach)
          S[static_cast<size_t>(b)].last_seen = state.day;
      i32 next = -1;
      if (bk.businesses.size() > 1 || m.target < 0) {
        for (i32 tries = 0; tries < 6 && (next < 0 || next == m.target); ++tries)
          next = bk.businesses.empty() ? -1 : bk.businesses[static_cast<size_t>(gang_rng.range(0, static_cast<i32>(bk.businesses.size()) - 1))];
      }
      const vec2 goal = next >= 0 ? world().businesses[static_cast<size_t>(next)].door : bk.centroid;
      m.target = next;
      if (!path_to(m, goal)) {
        path_to(m, hq.door);
        m.task = job::returning;
      }
      set_act(m, act::walk);
    }
    break;
  }
  case job::returning:
    if (distance(m.pos, hq.door) < arrive * 2.0f || m.agent.done()) {
      if (m.carrying > 0) {
        const city::business &bz = world().businesses[static_cast<size_t>(m.target)];
        const std::string from = m.sent_on == errand::raid && g.raid.victim >= 0
                                     ? "địa bàn " + G[static_cast<size_t>(g.raid.victim)].name
                                     : bz.name;
        g.money += m.carrying;
        m.morale = std::min(100.0f, m.morale + 1.0f);
        note(g, m.carrying, "Thu ở " + from);
        if (gi == 0) {
          char line[160];
          std::snprintf(line, sizeof(line), "%s mang về %dk từ %s", m.name.c_str(), m.carrying, from.c_str());
          ui_toast(ctx, line, {.kind = ui_toast_success});
        }
      }
      m.carrying = 0;
      m.target = -1;
      m.beat = -1;
      m.sent_on = errand::usual;
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
  focus_gi = focus_mi = -1;
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

bool gang_hire(context &ctx, i32 gi) {
  if (gi < 0 || gi >= static_cast<i32>(G.size()))
    return false;
  gang_state &g = G[static_cast<size_t>(gi)];
  if (g.money < recruit_cost)
    return false;
  g.money -= recruit_cost;
  add_man(ctx, gi, rank::soldier);
  seat_men(gi);
  step_in(ctx, g.men.back(), extras[static_cast<size_t>(gi)].back());
  note(g, -recruit_cost, "Tuyển " + g.men.back().name);
  return true;
}

bool gang_recruit(context &ctx) { return gang_hire(ctx, 0); }

bool gang_guards(i32 gi, i32 b) {
  if (gi < 0 || gi >= static_cast<i32>(G.size()) || b < 0 || b >= static_cast<i32>(world().businesses.size()))
    return false;
  const gang_state &g = G[static_cast<size_t>(gi)];
  const vec2 door = world().businesses[static_cast<size_t>(b)].door;
  constexpr f32 street_reach = 12.0f * city::units_per_metre;
  constexpr f32 home_reach = 25.0f * city::units_per_metre;
  i32 home = 0;
  for (const lackey &m : g.men) {
    if (!m.inside && distance(m.pos, door) <= street_reach)
      return true;
    if (m.inside)
      ++home;
  }
  return home >= 2 && distance(hq_of(g).door, door) <= home_reach;
}

void gang_muster(context &, bool out) {
  if (!G.empty())
    G[0].mustered = out;
}

bool gang_order(context &ctx, i32 gi, i32 mi, i32 b, errand e) {
  if (gi < 0 || gi >= static_cast<i32>(G.size()) || mi < 0 || mi >= static_cast<i32>(G[static_cast<size_t>(gi)].men.size()) ||
      b < 0 || b >= static_cast<i32>(world().businesses.size()))
    return false;
  gang_state &g = G[static_cast<size_t>(gi)];
  lackey &m = g.men[static_cast<size_t>(mi)];
  man_extra &x = extras[static_cast<size_t>(gi)][static_cast<size_t>(mi)];
  if (gang_cannot_go(m))
    return false;
  const bool was_inside = m.inside;
  step_out(ctx, m, g);
  if (!path_to(m, world().businesses[static_cast<size_t>(b)].door)) {
    if (was_inside && !g.mustered)
      step_in(ctx, m, x);
    return false;
  }
  m.target = b;
  m.sent_on = e;
  m.task = job::going;
  set_act(m, act::walk);
  x.check_at = m.pos;
  x.check_time = 0.0f;
  return true;
}

bool gang_send(context &ctx, i32 mi, i32 b) { return gang_order(ctx, 0, mi, b); }

bool gang_patrol(context &ctx, i32 gi, i32 mi, i32 block) {
  if (gi < 0 || gi >= static_cast<i32>(G.size()) || mi < 0 || mi >= static_cast<i32>(G[static_cast<size_t>(gi)].men.size()) ||
      block < 0 || block >= static_cast<i32>(world().blocks.size()))
    return false;
  gang_state &g = G[static_cast<size_t>(gi)];
  lackey &m = g.men[static_cast<size_t>(mi)];
  man_extra &x = extras[static_cast<size_t>(gi)][static_cast<size_t>(mi)];
  if (gang_cannot_go(m))
    return false;
  const bool was_inside = m.inside;
  step_out(ctx, m, g);
  const city::block &bk = world().blocks[static_cast<size_t>(block)];
  const i32 first = bk.businesses.empty() ? -1 : bk.businesses.front();
  if (!path_to(m, first >= 0 ? world().businesses[static_cast<size_t>(first)].door : bk.centroid)) {
    if (was_inside && !g.mustered)
      step_in(ctx, m, x);
    return false;
  }
  m.beat = block;
  m.target = first;
  m.sent_on = errand::usual;
  m.task = job::patrolling;
  set_act(m, act::walk);
  x.check_at = m.pos;
  x.check_time = 0.0f;
  return true;
}

bool gang_raid(context &ctx, i32 gi, i32 mi, i32 b) {
  if (gi < 0 || gi >= static_cast<i32>(G.size()) || b < 0 || b >= static_cast<i32>(S.size()))
    return false;
  const i32 victim = S[static_cast<size_t>(b)].owner;
  const i32 block = world().businesses[static_cast<size_t>(b)].block;
  if (victim < 0 || victim == gi || block < 0)
    return false;
  gang_state &g = G[static_cast<size_t>(gi)];
  // A new raid unless this joins the one going on in that block.
  if (!(g.raid.on && g.raid.block == block && g.raid.victim == victim)) {
    raid_plan r;
    r.on = true;
    r.victim = victim;
    r.block = block;
    r.day = state.day;
    for (const i32 o : world().blocks[static_cast<size_t>(block)].businesses)
      if (S[static_cast<size_t>(o)].owner == victim)
        r.quota += std::max(20, world().businesses[static_cast<size_t>(o)].protection / 7);
    const raid_plan old = g.raid;
    g.raid = r;
    if (!gang_order(ctx, gi, mi, b, errand::raid)) {
      g.raid = old;
      return false;
    }
    return true;
  }
  return gang_order(ctx, gi, mi, b, errand::raid);
}

bool gang_recall(context &ctx, i32 gi, i32 mi) {
  if (gi < 0 || gi >= static_cast<i32>(G.size()) || mi < 0 || mi >= static_cast<i32>(G[static_cast<size_t>(gi)].men.size()))
    return false;
  gang_state &g = G[static_cast<size_t>(gi)];
  lackey &m = g.men[static_cast<size_t>(mi)];
  if (m.task == job::idle || m.task == job::returning)
    return false;
  path_to(m, hq_of(g).door);
  m.task = job::returning;
  set_act(m, act::walk);
  if (gi == 0) {
    char line[160];
    std::snprintf(line, sizeof(line), "%s rút về trụ sở", m.name.c_str());
    ui_toast(ctx, line);
  }
  (void)ctx;
  return true;
}

void gang_step(context &ctx, f32 dt) {
  if (G.empty())
    return;
  const f32 speed = clock_speed();
  if (clock_new_day())
    new_day(ctx);
  wear(dt * speed / seconds_per_hour);
  for (i32 gi = 0; gi < static_cast<i32>(G.size()); ++gi)
    for (i32 i = 0; i < static_cast<i32>(G[static_cast<size_t>(gi)].men.size()); ++i)
      step_man(ctx, gi, i, dt, speed);
  drop_quitters(ctx);
}

void gang_update(f32 dt) {
  const f32 speed = clock_speed();
  for (gang_state &g : G)
    for (lackey &m : g.men) {
      const f32 pace = m.now == act::walk ? clamp(m.speed / walk_pace, 0.25f, 3.5f) : std::max(speed, 0.2f);
      m.time += dt * pace;
      m.was_time += dt;
      m.blend = std::max(0.0f, m.blend - dt / blend_time);
    }
}

namespace {
void draw_man(context &ctx, const gang_state &g, const lackey &m) {
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
} // namespace

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
    // The men of the open floor, if any, are seen inside.
    const i32 open_floor = open_floor_of(g);
    for (const lackey &m : g.men) {
      if (m.inside && m.floor != open_floor)
        continue;
      if (!city::view_sees(m.pos, 20.0f))
        continue;
      draw_man(ctx, g, m);
    }
  }
  material3d_set(ctx, {});
}

void gang_draw_around(context &ctx, vec2 at, f32 range) {
  if (!person_ready())
    return;
  material3d_set(ctx, {.specular = 0.15f, .shininess = 16.0f});
  for (const gang_state &g : G)
    for (const lackey &m : g.men)
      if (!m.inside && distance(m.pos, at) <= range)
        draw_man(ctx, g, m);
  material3d_set(ctx, {});
}

void gang_focus_man(i32 gi, i32 mi) {
  focus_gi = gi;
  focus_mi = mi;
}

void gang_unfocus_man() { focus_gi = focus_mi = -1; }

bool gang_focused_pos(vec2 &out, f32 &lift) {
  if (focus_gi < 0 || focus_gi >= static_cast<i32>(G.size()))
    return false;
  const gang_state &g = G[static_cast<size_t>(focus_gi)];
  if (focus_mi < 0 || focus_mi >= static_cast<i32>(g.men.size()))
    return false;
  const lackey &m = g.men[static_cast<size_t>(focus_mi)];
  if (m.inside && m.floor != open_floor_of(g))
    return false;
  out = m.pos;
  lift = m.inside ? static_cast<f32>(m.floor) * city::floor_height : 0.0f;
  return true;
}

bool gang_pick_man(context &ctx, vec2 screen, f32 max_px, i32 &out_gi, i32 &out_mi) {
  if (state.cam_distance > 60.0f) // past this gang_draw() itself draws none
    return false;
  f32 best = max_px;
  bool found = false;
  for (i32 gi = 0; gi < static_cast<i32>(G.size()); ++gi) {
    const gang_state &g = G[static_cast<size_t>(gi)];
    const i32 open_floor = open_floor_of(g);
    for (i32 mi = 0; mi < static_cast<i32>(g.men.size()); ++mi) {
      const lackey &m = g.men[static_cast<size_t>(mi)];
      if (m.inside && m.floor != open_floor)
        continue;
      if (!city::view_sees(m.pos, 20.0f))
        continue;
      bool visible = false;
      const vec2 s = table_to_screen(ctx, m.pos, 0.9f, &visible);
      if (!visible)
        continue;
      const f32 d = distance(s, screen);
      if (d < best) {
        best = d;
        out_gi = gi;
        out_mi = mi;
        found = true;
      }
    }
  }
  return found;
}

i32 gang_income_per_day() {
  const city::city_map &map = world();
  i32 sum = 0;
  for (size_t i = 0; i < map.businesses.size() && i < S.size(); ++i)
    if (S[i].owner == 0)
      sum += std::max(1, map.businesses[i].protection / 7);
  return sum;
}

i32 living_cost(rank r) {
  switch (r) {
  case rank::boss: return 0; // he lives at the headquarters, on the gang
  case rank::deputy: return 140;
  case rank::captain: return 100;
  default: return 70; // a bowl of rice twice a day and a shared room
  }
}

const char *mood_name(f32 morale) {
  return morale >= 75.0f ? "Hăng hái" : morale >= 50.0f ? "Ổn" : morale >= quit_morale ? "Bực bội" : "Muốn bỏ";
}

const char *gang_cannot_go(const lackey &m) {
  if (m.task != job::idle)
    return "Đang bận";
  if (m.quitting)
    return "Sắp bỏ đi";
  if (m.health < hurt_limit)
    return "Bị thương";
  if (m.fatigue >= tired_limit)
    return "Mệt";
  return nullptr;
}

i32 gang_clinic_per_day() {
  i32 sum = 0;
  if (!G.empty())
    for (const lackey &m : G[0].men)
      sum += m.health < 100.0f && m.treat ? clinic_cost : 0;
  return sum;
}

i32 gang_wages_owed() {
  i32 sum = 0;
  if (!G.empty())
    for (const lackey &m : G[0].men)
      sum += m.unpaid;
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
  case job::patrolling: return "Đang tuần tra";
  }
  return "";
}

const char *gang_target_name(const lackey &m) {
  if (m.task == job::patrolling && m.beat >= 0)
    return world().districts[static_cast<size_t>(world().blocks[static_cast<size_t>(m.beat)].district)].name.c_str();
  if (m.target < 0)
    return "";
  return world().businesses[static_cast<size_t>(m.target)].name.c_str();
}

} // namespace sandtable
