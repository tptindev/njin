#include <njin.h>

// Hai người chơi cùng một mô phỏng tất định: một con số và một bộ đếm, đổi theo
// lệnh của từng người. Không máy nào gửi trạng thái; mọi máy làm theo đúng các lệnh
// đó, ở đúng tick đó, nên con số giống nhau trên mọi máy. Cùng module chạy được cả
// hai vai: lockstep_host() hay lockstep_join() ở setup.
namespace {
njin::u64 state = 1; // toàn bộ "thế giới"
njin::i32 score[4] = {};

void tick(njin::context &ctx) {
  if (!njin::lockstep_step(ctx))
    return; // lệnh của tick sau chưa đến: thế giới đứng yên nhịp này
  for (const njin::lockstep_command &c : njin::lockstep_commands(ctx)) {
    njin::bit_reader r(c.data);
    const njin::i32 add = r.read_int(1, 9);
    if (r.ok() && c.player < 4) {
      score[c.player] += add;
      state ^= (njin::u64)add << (c.player * 8);
    }
  }
  state = state * 6364136223846793005ull + 1442695040888963407ull; // mô phỏng của tick
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
      njin::lockstep_start(ctx, ""); // đủ hai người: bắt đầu (setup: seed, bản đồ...)
    if (e.kind == njin::lockstep_event_kind::desync)
      NJIN_WARN("lệch ở tick %u", e.tick);
  }
  // Bấm Space: một lệnh. Không tự cộng điểm ở đây: làm theo nó khi nó đến, như máy kia.
  if (njin::key_pressed(ctx, njin::key_space)) {
    njin::bit_writer w;
    w.write_int(3, 1, 9);
    njin::lockstep_send(ctx, w.data());
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_fixed_update, tick, "tick");
  njin::ecs_register(ctx, njin::phase_update, update, "update");
  njin::lockstep_host(ctx, 47100); // máy kia: njin::lockstep_join(ctx, "192.168.1.20", 47100)
}
} // namespace

njin::mod_desc lockstep_demo() { return {.name = "lockstep_demo", .setup = setup}; }
