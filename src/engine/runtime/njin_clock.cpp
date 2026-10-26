#include "njin_ctx.h"
#include <chrono>
#include <ctime>

// The machine's wall clock (time_unix and the date helpers in njin_ctx.h).
// UTC goes through <chrono>'s civil calendar; local time through the C
// library, which knows the machine's time zone and daylight saving.
namespace njin {
namespace chr = std::chrono;

namespace {

date_time from_tm(const std::tm &t) {
  return {.year = t.tm_year + 1900,
          .month = t.tm_mon + 1,
          .day = t.tm_mday,
          .hour = t.tm_hour,
          .minute = t.tm_min,
          .second = t.tm_sec,
          .weekday = t.tm_wday};
}

// Floor division, for months below 1 or above 12.
i64 floor_div(i64 a, i64 b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }
} // namespace

i64 time_unix() { return chr::duration_cast<chr::seconds>(chr::system_clock::now().time_since_epoch()).count(); }

date_time time_date(i64 unix_time, bool local) {
  if (local) {
    const std::time_t at = (std::time_t)unix_time;
    std::tm t{};
#if defined(_WIN32)
    localtime_s(&t, &at);
#else
    localtime_r(&at, &t);
#endif
    return from_tm(t);
  }
  const chr::sys_seconds at{chr::seconds{unix_time}};
  const chr::sys_days day = chr::floor<chr::days>(at);
  const chr::year_month_day ymd{day};
  const chr::hh_mm_ss<chr::seconds> hms{at - day};
  return {.year = (i32)ymd.year(),
          .month = (i32)(unsigned)ymd.month(),
          .day = (i32)(unsigned)ymd.day(),
          .hour = (i32)hms.hours().count(),
          .minute = (i32)hms.minutes().count(),
          .second = (i32)hms.seconds().count(),
          .weekday = (i32)chr::weekday{day}.c_encoding()};
}

i64 time_from_date(const date_time &date, bool local) {
  if (local) {
    std::tm t{};
    t.tm_year = date.year - 1900;
    t.tm_mon = date.month - 1;
    t.tm_mday = date.day;
    t.tm_hour = date.hour;
    t.tm_min = date.minute;
    t.tm_sec = date.second;
    t.tm_isdst = -1; // the C library decides whether daylight saving applies
    return (i64)std::mktime(&t);
  }
  // Fields out of range carry over: the month into the year here, the rest by
  // adding them up as durations.
  const i64 months = (i64)date.month - 1;
  const i64 y = (i64)date.year + floor_div(months, 12);
  const i64 m = months - floor_div(months, 12) * 12 + 1;
  const chr::sys_days first = chr::year_month_day{chr::year{(i32)y}, chr::month{(unsigned)m}, chr::day{1}};
  const chr::sys_seconds at = first + chr::days{date.day - 1} + chr::hours{date.hour} + chr::minutes{date.minute} +
                              chr::seconds{date.second};
  return at.time_since_epoch().count();
}
} // namespace njin
