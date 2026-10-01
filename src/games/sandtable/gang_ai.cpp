#include "gang_ai.h"
#include "clock.h"
#include "gang.h"
#include "world.h"

#include <algorithm>
#include <cstdio>

namespace sandtable {

namespace {

rng ai_rng(20240917u);

// The hours the rivals' men go out on business.
constexpr f32 work_from = 8.0f, work_to = 23.0f;
// Raids start in the busy hours, when shops are open and have takings.
constexpr f32 raid_from = 10.0f, raid_to = 21.0f;
constexpr f32 raid_chance = 0.12f;   // each such hour, for a gang that has not raided today
constexpr f32 spread_chance = 0.2f;  // each working hour
constexpr i32 keep_home = 2;         // men a boss keeps in, to hold the headquarters
constexpr i32 most_men = 12;

// Men of gang `gi` free for a job: not the boss, not the right hand, not
// out on one, not worn out or hurt.
std::vector<i32> free_men(const gang_state &g) {
  std::vector<i32> out;
  for (i32 i = 0; i < static_cast<i32>(g.men.size()); ++i) {
    const lackey &m = g.men[static_cast<size_t>(i)];
    if (!gang_cannot_go(m) && (m.rk == rank::soldier || m.rk == rank::captain))
      out.push_back(i);
  }
  return out;
}

bool someone_sent(const gang_state &g, i32 b) {
  for (const lackey &m : g.men)
    if (m.task != job::idle && m.target == b)
      return true;
  return false;
}

// Takes the free man nearest `at` off `free` and sends him; false if no one
// could go.
bool send_nearest(context &ctx, i32 gi, std::vector<i32> &free, i32 b, errand e) {
  const gang_state &g = gangs()[static_cast<size_t>(gi)];
  const vec2 at = world().businesses[static_cast<size_t>(b)].door;
  std::sort(free.begin(), free.end(), [&](i32 a, i32 c) {
    return distance(g.men[static_cast<size_t>(a)].pos, at) < distance(g.men[static_cast<size_t>(c)].pos, at);
  });
  for (size_t k = 0; k < free.size(); ++k)
    if (gang_order(ctx, gi, free[k], b, e)) {
      free.erase(free.begin() + static_cast<std::ptrdiff_t>(k));
      return true;
    }
  return false;
}

// A man to every open shop of theirs that owes a day or more.
void collect(context &ctx, i32 gi, std::vector<i32> &free) {
  const city::city_map &map = world();
  std::vector<i32> due;
  for (i32 b = 0; b < static_cast<i32>(map.businesses.size()); ++b) {
    const shop_state &s = shops()[static_cast<size_t>(b)];
    if (s.owner == gi && s.owed >= std::max(1, map.businesses[static_cast<size_t>(b)].protection / 7) &&
        business_open(b) && !someone_sent(gangs()[static_cast<size_t>(gi)], b))
      due.push_back(b);
  }
  std::sort(due.begin(), due.end(),
            [](i32 a, i32 c) { return shops()[static_cast<size_t>(a)].owed > shops()[static_cast<size_t>(c)].owed; });
  for (const i32 b : due) {
    if (static_cast<i32>(free.size()) <= keep_home)
      return;
    send_nearest(ctx, gi, free, b, errand::usual);
  }
}

// A block of the player's turf to raid: the one with most of his shops open
// now, nearer counting for more. -1 if none.
i32 raid_target(i32 gi) {
  const city::city_map &map = world();
  const gang_state &g = gangs()[static_cast<size_t>(gi)];
  const vec2 home = map.buildings[static_cast<size_t>(g.hq)].door;
  i32 best = -1;
  f32 best_score = 0.0f;
  for (i32 k = 0; k < static_cast<i32>(map.blocks.size()); ++k) {
    i32 open = 0;
    for (const i32 b : map.blocks[static_cast<size_t>(k)].businesses)
      if (shops()[static_cast<size_t>(b)].owner == 0 && business_open(b))
        ++open;
    if (open == 0)
      continue;
    const f32 d = distance(map.blocks[static_cast<size_t>(k)].centroid, home);
    const f32 score = static_cast<f32>(open) / (1.0f + d / 400.0f);
    if (score > best_score) {
      best_score = score;
      best = k;
    }
  }
  return best;
}

void raid(context &ctx, i32 gi, std::vector<i32> &free) {
  gang_state &g = gangs()[static_cast<size_t>(gi)];
  if (g.raid.day == state.day || g.raid.victim >= 0 || gangs().empty())
    return;
  if (!hour_between(state.hour, raid_from, raid_to) || static_cast<i32>(free.size()) < keep_home + 2)
    return;
  if (!ai_rng.chance(raid_chance))
    return;
  const i32 k = raid_target(gi);
  if (k < 0)
    return;
  const city::city_map &map = world();
  std::vector<i32> targets;
  i32 worth = 0;
  for (const i32 b : map.blocks[static_cast<size_t>(k)].businesses)
    if (shops()[static_cast<size_t>(b)].owner == 0) {
      worth += std::max(20, map.businesses[static_cast<size_t>(b)].protection / 7);
      if (business_open(b))
        targets.push_back(b);
    }
  g.raid = {};
  g.raid.on = true;
  g.raid.victim = 0;
  g.raid.block = k;
  g.raid.day = state.day;
  g.raid.quota = std::max(60, worth * 6 / 10);
  const i32 raiders = std::min({3, static_cast<i32>(free.size()) - keep_home, static_cast<i32>(targets.size())});
  i32 sent = 0;
  for (i32 r = 0; r < raiders; ++r)
    if (send_nearest(ctx, gi, free, targets[static_cast<size_t>(r)], errand::raid))
      ++sent;
  if (sent == 0) {
    g.raid = {};
    g.raid.day = state.day;
    return;
  }
  const city::block &bk = map.blocks[static_cast<size_t>(k)];
  NJIN_INFO("[ai] %s: %d men raid %s (block %d), quota %dk", g.name.c_str(), sent,
            map.districts[static_cast<size_t>(bk.district)].name.c_str(), k, g.raid.quota);
}

// Now and then, a shop that pays no one, next to their turf.
void spread(context &ctx, i32 gi, std::vector<i32> &free) {
  if (static_cast<i32>(free.size()) <= keep_home || !ai_rng.chance(spread_chance))
    return;
  const city::city_map &map = world();
  const gang_state &g = gangs()[static_cast<size_t>(gi)];
  const i32 home_block = map.buildings[static_cast<size_t>(g.hq)].block;
  std::vector<i32> near;
  for (i32 k = 0; k < static_cast<i32>(map.blocks.size()); ++k) {
    if (turf_owner()[static_cast<size_t>(k)] != gi && k != home_block)
      continue;
    near.push_back(k);
    for (const i32 n : map.blocks[static_cast<size_t>(k)].neighbours)
      near.push_back(n);
  }
  std::vector<i32> cand;
  for (const i32 k : near)
    for (const i32 b : map.blocks[static_cast<size_t>(k)].businesses)
      if (shops()[static_cast<size_t>(b)].owner < 0 && business_open(b) && !someone_sent(g, b))
        cand.push_back(b);
  if (cand.empty())
    return;
  const i32 b = cand[static_cast<size_t>(ai_rng.range(0, static_cast<i32>(cand.size()) - 1))];
  if (send_nearest(ctx, gi, free, b, errand::usual))
    NJIN_INFO("[ai] %s: squeezing %s", g.name.c_str(), map.businesses[static_cast<size_t>(b)].name.c_str());
}

// A raid done (everyone home): a while later a shopkeeper tells the boss of
// the gang raided.
void follow_raid(context &ctx, i32 gi) {
  gang_state &g = gangs()[static_cast<size_t>(gi)];
  raid_plan &r = g.raid;
  if (r.victim < 0)
    return;
  if (r.report_at < 0.0) {
    for (const lackey &m : g.men)
      if (m.sent_on == errand::raid)
        return; // still out
    r.on = false;
    if (r.taken <= 0) {
      r.victim = -1;
      return;
    }
    r.report_at = clock_now() + static_cast<f64>(ai_rng.range(0.5f, 2.0f));
    return;
  }
  if (clock_now() < r.report_at)
    return;
  const city::city_map &map = world();
  i32 told = -1, paid = 0;
  for (const i32 b : r.hit) {
    const shop_state &s = shops()[static_cast<size_t>(b)];
    if (s.raided_by == gi && s.raided_day == r.day) {
      ++paid;
      told = b;
    }
  }
  if (told >= 0 && r.victim == 0) {
    const city::business &bz = map.businesses[static_cast<size_t>(told)];
    char line[260];
    std::snprintf(line, sizeof(line), "Chủ %s báo: người của %s vừa sang thu %dk ở %d quán của mình", bz.name.c_str(),
                  g.name.c_str(), r.taken, paid);
    ui_toast(ctx, line, {.kind = ui_toast_error, .seconds = 8.0f});
    NJIN_INFO("[ai] report to the player: %s", line);
  }
  r.victim = -1;
  r.report_at = -1.0;
}

} // namespace

void gang_ai_step(context &ctx) {
  std::vector<gang_state> &all = gangs();
  if (all.size() < 2 || clock_speed() <= 0.0f)
    return;
  for (i32 gi = 1; gi < static_cast<i32>(all.size()); ++gi)
    follow_raid(ctx, gi);
  if (clock_new_day())
    for (i32 gi = 1; gi < static_cast<i32>(all.size()); ++gi) {
      gang_state &g = all[static_cast<size_t>(gi)];
      if (g.money >= recruit_cost * 4 && static_cast<i32>(g.men.size()) < most_men && gang_hire(ctx, gi))
        NJIN_INFO("[ai] %s hires %s", g.name.c_str(), g.men.back().name.c_str());
    }
  if (!clock_new_hour() || !hour_between(state.hour, work_from, work_to))
    return;
  for (i32 gi = 1; gi < static_cast<i32>(all.size()); ++gi) {
    std::vector<i32> free = free_men(all[static_cast<size_t>(gi)]);
    raid(ctx, gi, free);
    collect(ctx, gi, free);
    spread(ctx, gi, free);
  }
}

} // namespace sandtable
