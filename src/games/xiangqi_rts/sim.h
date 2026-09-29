#pragma once

#include "types.h"

namespace xiangqi {

void sim_init(njin_ctx &ctx);
void sim_reset(njin_ctx &ctx);
void sim_update(njin_ctx &ctx);

entt::entity spawn_unit(njin_ctx &ctx, piece_type type, faction side, vec2 pos);
bool recruit_unit(njin_ctx &ctx, piece_type type);
void trigger_rally(njin_ctx &ctx);

void issue_move_order(njin_ctx &ctx, const std::vector<entt::entity> &units, vec2 target_pos, bool attack_move = false);
void issue_attack_order(njin_ctx &ctx, const std::vector<entt::entity> &units, entt::entity target);
void stop_units(njin_ctx &ctx, const std::vector<entt::entity> &units);

bool is_point_on_bridge(vec2 pos);
bool is_point_in_river(vec2 pos);

void add_damage_popup(vec2 pos, f32 amount, rgba col, bool crit = false, const char *txt = nullptr);
void add_particle(vec2 pos, vec2 vel, rgba col, f32 size = 4.0f, f32 life = 0.4f, bool ring = false);

} // namespace xiangqi
