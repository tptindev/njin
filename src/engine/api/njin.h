#pragma once

namespace njin {
struct njin_cfg;
struct njin_ctx;
void njin_init(njin_ctx &ctx, const njin_cfg &cfg);
void njin_run(njin_ctx &ctx);
void njin_shutdown(njin_ctx &ctx);
} // namespace njin
