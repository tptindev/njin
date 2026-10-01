#include "clock.h"
#include "world.h"

#include <cmath>

namespace sandtable {

namespace {
bool passed_day = false, passed_hour = false;
} // namespace

f32 clock_speed() { return state.popup_open ? 0.0f : state.speed; }

f64 clock_now() { return static_cast<f64>(state.day - 1) * 24.0 + static_cast<f64>(state.hour); }

bool clock_new_day() { return passed_day; }
bool clock_new_hour() { return passed_hour; }

void clock_step(f32 dt) {
  const f32 was = state.hour;
  state.hour += dt * clock_speed() / seconds_per_hour;
  passed_day = state.hour >= 24.0f;
  if (passed_day) {
    state.hour -= 24.0f;
    ++state.day;
  }
  passed_hour = passed_day || std::floor(state.hour) != std::floor(was);
}

bool hour_between(f32 h, f32 from, f32 to) {
  if (from == to)
    return true;
  return from < to ? h >= from && h < to : h >= from || h < to;
}

opening business_hours(city::business_kind k) {
  using bk = city::business_kind;
  switch (k) {
  case bk::cafe: return {6.0f, 22.0f};
  case bk::street_food: return {6.0f, 23.5f};
  case bk::restaurant: return {10.0f, 22.0f};
  case bk::grocery: return {6.0f, 22.0f};
  case bk::pharmacy: return {7.0f, 22.0f};
  case bk::gold_shop: return {8.0f, 20.0f};
  case bk::pawn_shop: return {8.0f, 21.0f};
  case bk::karaoke: return {14.0f, 2.0f};
  case bk::bar: return {18.0f, 3.0f};
  case bk::billiards: return {9.0f, 0.0f};
  case bk::massage: return {10.0f, 23.0f};
  case bk::guest_house: return {0.0f, 0.0f};
  case bk::hotel: return {0.0f, 0.0f};
  case bk::bike_repair: return {7.0f, 19.0f};
  case bk::gas_station: return {5.0f, 23.0f};
  case bk::market: return {4.0f, 18.0f};
  case bk::warehouse: return {7.0f, 18.0f};
  case bk::workshop: return {7.0f, 18.0f};
  case bk::gambling_den: return {20.0f, 5.0f};
  case bk::count: break;
  }
  return {8.0f, 20.0f};
}

bool business_open_at(i32 i, f32 hour) {
  const city::city_map &m = world();
  if (i < 0 || i >= static_cast<i32>(m.businesses.size()))
    return false;
  const opening o = business_hours(m.businesses[static_cast<size_t>(i)].kind);
  return hour_between(hour, o.open, o.close);
}

bool business_open(i32 i) { return business_open_at(i, state.hour); }

} // namespace sandtable
