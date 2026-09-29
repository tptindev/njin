#pragma once
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_body
/// @{

/// Nút và cần điều khiển của một njin::platformer_body, game ghi vào mỗi frame.
///
/// `jump` và `drop` là **yêu cầu**: đặt `true` ở frame bấm, bộ điều khiển tự
/// xóa khi đã xử lý, nên không bị lỡ dù frame đó không có nhịp vật lý nào.
/// njin::platformer_input_map ghi các trường này từ action cho bạn.
struct platformer_input {
  f32 move_x = 0.0f;      ///< Hướng chạy, -1 (trái) đến 1 (phải).
  bool jump = false;      ///< Yêu cầu nhảy. Đặt ở frame bấm nút nhảy.
  bool jump_held = false; ///< Nút nhảy đang được giữ. Thả sớm thì nhảy thấp.
  bool drop = false;      ///< Yêu cầu nhảy xuống khỏi bục một chiều đang đứng.
};

/// Nhân vật platformer: chạy, nhảy, rơi, trèo dốc, đứng trên bục di chuyển.
///
/// Cần transform và njin::collider hộp trên cùng entity. Module body của
/// engine, trong `phase_fixed_update`, đọc `input`, tính `velocity` và di
/// chuyển entity bằng collision_move(), rồi ghi lại trạng thái (`grounded`,
/// `on_wall`...). Game chỉ cần đặt input và đọc trạng thái để chọn animation.
///
/// Các cảm giác quen thuộc của platformer đều có sẵn:
/// - **coyote time**: rời mép rồi vẫn nhảy được trong `coyote_time` giây;
/// - **jump buffer**: bấm nhảy sớm trước khi chạm đất trong `jump_buffer` giây
///   vẫn được tính;
/// - **nhảy thấp khi thả sớm**: thả nút lúc đang lên thì vận tốc nhân `jump_cut`;
/// - rơi nhanh hơn lên (`fall_gravity`), giới hạn tốc độ rơi (`max_fall`);
/// - nhảy trên không (`air_jumps`), trượt tường và nhảy tường (tắt mặc định);
/// - dính dốc khi đi xuống dốc, nhảy xuyên xuống bục một chiều (`input.drop`).
///
/// Muốn đẩy lùi khi trúng đòn thì ghi thẳng vào `velocity`.
///
/// Mọi đơn vị là pixel và giây. Gửi event njin::body_jumped và
/// njin::body_landed qua events().
struct platformer_body {
  // --- chỉnh cảm giác ---
  f32 run_speed = 110.0f;     ///< Tốc độ chạy tối đa.
  f32 ground_accel = 1200.0f; ///< Tăng tốc trên đất.
  f32 ground_decel = 1600.0f; ///< Giảm tốc trên đất khi thả nút (hoặc đổi hướng).
  f32 air_accel = 800.0f;     ///< Tăng tốc trên không.
  f32 air_decel = 500.0f;     ///< Giảm tốc trên không.
  f32 gravity = 1000.0f;      ///< Gia tốc rơi khi đang lên.
  f32 fall_gravity = 1600.0f; ///< Gia tốc rơi khi đang xuống. Lớn hơn `gravity` cho cú nhảy chắc tay.
  f32 max_fall = 360.0f;      ///< Tốc độ rơi tối đa.
  f32 jump_speed = 300.0f;    ///< Vận tốc lên lúc bắt đầu nhảy.
  f32 jump_cut = 0.45f;       ///< Nhân vào vận tốc lên khi thả nút nhảy sớm. 1 là tắt.
  f32 coyote_time = 0.09f;    ///< Giây sau khi rời mép vẫn nhảy được.
  f32 jump_buffer = 0.12f;    ///< Giây nhớ lệnh nhảy bấm sớm.
  i32 air_jumps = 0;          ///< Số lần nhảy thêm trên không. 1 là nhảy đôi.
  f32 wall_slide_speed = 0.0f; ///< Tốc độ trượt tường khi áp vào tường lúc rơi. 0 là tắt.
  vec2 wall_jump{0.0f, 0.0f};  ///< Vận tốc khi nhảy khỏi tường (x ra xa tường, y lên). 0 là tắt.
  f32 wall_jump_lock = 0.15f;  ///< Giây bỏ qua `move_x` sau khi nhảy tường, để bật ra được.

  platformer_input input{}; ///< Điều khiển, game ghi vào.

  // --- trạng thái, engine ghi ---
  vec2 velocity{};       ///< Vận tốc hiện tại. Game được ghi (đẩy lùi, lò xo).
  bool grounded = false; ///< Đang đứng trên đất, dốc hoặc bục.
  bool on_slope = false; ///< Đang đứng trên dốc.
  i32 on_wall = 0;       ///< -1 áp tường bên trái, 1 bên phải, 0 không.
  i32 facing = 1;        ///< Hướng nhìn: -1 trái, 1 phải. Theo `input.move_x`.
  entt::entity ground = entt::null; ///< Entity đang đứng lên (tilemap hoặc bục).
  bool jumped = false;   ///< Vừa nhảy trong nhịp vật lý cuối cùng.
  bool landed = false;   ///< Vừa chạm đất trong nhịp vật lý cuối cùng.

  // --- bên trong ---
  f32 coyote_timer = 0.0f;   ///< Thời gian coyote còn lại.
  f32 buffer_timer = 0.0f;   ///< Thời gian lệnh nhảy còn được nhớ.
  f32 drop_timer = 0.0f;     ///< Thời gian còn xuyên bục một chiều.
  f32 wall_lock_timer = 0.0f; ///< Thời gian còn bỏ qua `move_x` sau nhảy tường.
  i32 air_jumps_left = 0;    ///< Số lần nhảy trên không còn lại.
  bool rising = false;       ///< Đang lên từ một cú nhảy mà nút còn được giữ.
  bool ground_one_way = false; ///< Mặt đang đứng là bục một chiều.
};

/// Nút và cần điều khiển của một njin::topdown_body.
struct topdown_input {
  vec2 move{};       ///< Hướng đi. Độ dài lớn hơn 1 được đưa về 1, nên đi chéo không nhanh hơn.
  bool dash = false; ///< Yêu cầu lướt. Đặt ở frame bấm; bộ điều khiển tự xóa.
};

/// Nhân vật top-down: đi 8 hướng (hoặc mọi hướng với cần analog), trượt dọc
/// tường, lướt.
///
/// Cần transform và njin::collider (hộp hoặc tròn). Module body của engine di
/// chuyển nó trong `phase_fixed_update` bằng collision_move(). Ghi vào
/// `velocity` để đẩy lùi. Gửi njin::body_dashed khi bắt đầu lướt.
struct topdown_body {
  f32 speed = 90.0f;   ///< Tốc độ đi tối đa.
  f32 accel = 900.0f;  ///< Tăng tốc.
  f32 decel = 1300.0f; ///< Giảm tốc khi thả nút.
  f32 dash_speed = 0.0f;     ///< Tốc độ lướt. 0 là tắt lướt.
  f32 dash_time = 0.14f;     ///< Thời gian một lần lướt, giây.
  f32 dash_cooldown = 0.35f; ///< Giây phải chờ giữa hai lần lướt, tính từ lúc bắt đầu.

  topdown_input input{}; ///< Điều khiển, game ghi vào.

  vec2 velocity{};        ///< Vận tốc hiện tại. Game được ghi.
  vec2 facing{0.0f, 1.0f}; ///< Hướng nhìn, độ dài 1: hướng đi gần nhất khác 0.
  bool moving = false;    ///< Đang đi (vận tốc khác 0).
  bool dashing = false;   ///< Đang lướt.
  f32 dash_timer = 0.0f;  ///< Thời gian lướt còn lại.
  f32 cooldown_timer = 0.0f; ///< Thời gian chờ lướt còn lại.
};

/// Gắn action và axis vào một njin::platformer_body: module body đọc chúng
/// trong `phase_pre_update` và ghi `input`, nên game không cần tự làm.
///
/// Nhảy xuống khỏi bục một chiều: giữ `down` rồi bấm `jump`.
struct platformer_input_map {
  axis_handle move{};     ///< Trục ngang (phím trái/phải, cần trái).
  action_handle jump{};   ///< Nhảy.
  action_handle down{};   ///< Giữ để nhảy xuống khỏi bục. Có thể để trống.
};

/// Gắn action và axis vào một njin::topdown_body, như njin::platformer_input_map.
struct topdown_input_map {
  axis_handle move_x{}; ///< Trục ngang.
  axis_handle move_y{}; ///< Trục dọc (dương là xuống).
  action_handle dash{}; ///< Lướt. Có thể để trống.
};

/// Cho một entity đi theo một đường gấp khúc lặp lại: bục di chuyển, lính gác,
/// cưa chạy ray.
///
/// Module body của engine, trong `phase_fixed_update` (trước nhân vật), dời
/// entity về điểm kế tiếp với tốc độ `speed`. Entity có collider hộp thì được
/// dời bằng collision_move_platform(), nên chở theo nhân vật đứng trên nó.
struct path_mover {
  std::vector<vec2> points; ///< Các điểm, trong thế giới. Ít nhất hai.
  f32 speed = 40.0f;        ///< Tốc độ, pixel mỗi giây.
  f32 wait = 0.0f;          ///< Giây dừng lại ở mỗi điểm.
  /// `true`: đi hết rồi quay về điểm đầu (vòng kín). `false`: đi hết rồi đi
  /// ngược lại (tới lui).
  bool loop = false;
  bool paused = false;      ///< Dừng tạm.
  i32 target = 1;           ///< Điểm đang đi tới.
  i32 direction = 1;        ///< Chiều đi qua danh sách, khi `loop` là `false`.
  f32 wait_timer = 0.0f;    ///< Thời gian dừng còn lại.
};

/// Event: một njin::platformer_body vừa nhảy (cả nhảy trên không, nhảy tường).
struct body_jumped {
  entt::entity entity = entt::null; ///< Nhân vật.
  bool from_air = false;  ///< Nhảy trên không (nhảy đôi).
  bool from_wall = false; ///< Nhảy khỏi tường.
};

/// Event: một njin::platformer_body vừa chạm đất.
struct body_landed {
  entt::entity entity = entt::null; ///< Nhân vật.
  f32 speed = 0.0f; ///< Tốc độ rơi lúc chạm đất: để rung màn hình khi rơi mạnh.
};

/// Event: một njin::topdown_body vừa bắt đầu lướt.
struct body_dashed {
  entt::entity entity = entt::null; ///< Nhân vật.
  vec2 direction{}; ///< Hướng lướt, độ dài 1.
};
/// @}
} // namespace njin
