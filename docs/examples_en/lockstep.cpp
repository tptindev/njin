#include <njin.h>

// Two players, one deterministic simulation: a number and a counter, changed by
// each player's commands. No machine sends state; every machine applies the exact
// same commands, at the exact same tick, so the number matches on every machine.
// The same module runs either role: lockstep_host() or lockstep_join() in setup.
namespace {
njin::u64 state = 1; // the entire "world"
njin::i32 score[4] = {};

void tick(njin::context &ctx) {
  if (!njin::lockstep_step(ctx))
    return; // next tick's commands haven't arrived yet: the world stands still this step
  for (const njin::lockstep_command &c : njin::lockstep_commands(ctx)) {
    njin::bit_reader r(c.data);
    const njin::i32 add = r.read_int(1, 9);
    if (r.ok() && c.player < 4) {
      score[c.player] += add;
      state ^= (njin::u64)add << (c.player * 8);
    }
  }
  state = state * 6364136223846793005ull + 1442695040888963407ull; // the tick's simulation
  if (njin::lockstep_hash_due(ctx)) {
    njin::lockstep_hasher h;
    h.add(state);
    for (const njin::i32 s : score)
      h.add(s);
    njin::lockstep_hash(ctx, h.value);
  }
}

void update(njin::context &ctx) {
  njin::lockstep_event e;
  while (njin::lockstep_poll(ctx, e)) {
    if (e.kind == njin::lockstep_event_kind::joined && njin::lockstep_role_of(ctx) == njin::lockstep_role::host)
      njin::lockstep_start(ctx, ""); // two players now: start (setup: seed, map...)
    if (e.kind == njin::lockstep_event_kind::desync)
      NJIN_WARN("desynced at tick %u", e.tick);
  }
  // Press Space: one command. Don't add to the score here: apply it when it arrives, like the other machine does.
  if (njin::key_pressed(ctx, njin::key_space)) {
    njin::bit_writer w;
    w.write_int(3, 1, 9);
    njin::lockstep_send(ctx, w.data());
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_fixed_update, tick, "tick");
  njin::ecs_register(ctx, njin::phase_update, update, "update");
  njin::lockstep_host(ctx, 47100); // the other machine: njin::lockstep_join(ctx, "192.168.1.20", 47100)
}
} // namespace

njin::mod_desc lockstep_demo() { return {.name = "lockstep_demo", .setup = setup}; }
