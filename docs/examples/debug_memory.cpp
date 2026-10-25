#include <njin.h>

// [memory]
void run_game(njin::config cfg) {
  cfg.mem_budget_mb = 800;  // vượt thì gọi các hàm giải phóng
  cfg.mem_limit_mb = 1000;  // vượt thì cấp phát bị từ chối
  njin::context *ctx = njin::create(cfg);
  njin::mem_on_pressure([](njin::context &, const njin::mem_stats &stats) {
    NJIN_WARN("RAM %zu MB, bỏ cache", stats.used / (1024 * 1024));
    // bỏ texture không dùng, hạ chất lượng...
  });
  njin::run(*ctx);
  njin::destroy(ctx);
}
// [memory]
