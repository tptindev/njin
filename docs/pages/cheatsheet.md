# Muốn làm X thì dùng gì {#cheatsheet}

Bảng tra nhanh: tìm việc bạn muốn làm ở cột trái, dùng thứ ở cột giữa, đọc trang ở cột phải.
Mọi thứ nằm trong `njin::` và chỉ cần `#include <njin.h>`. Muốn xem đầy đủ tham số của một hàm thì bấm vào tên
nó, hoặc vào [Nhóm API](topics.html).

Chưa biết bắt đầu từ đâu? Đọc @ref first_jump (platformer) hoặc @ref first_walk (top-down) trước: một nhân
vật chạy được trong 50 dòng. Chưa quen `entt::registry`? Đọc @ref ecs.

## Khởi động và cấu trúc game

| Muốn | Dùng | Xem |
|---|---|---|
| Mở cửa sổ, chạy, đóng | njin::njin_create(), njin::njin_run(), njin::njin_destroy() | @ref getting_started |
| Thoát game từ trong game | njin::njin_quit() | @ref window_files |
| Gom logic của game vào một chỗ | njin::mod_desc với `setup`, đăng ký bằng njin::njin_mod_register() | @ref modules_systems |
| Chạy một hàm mỗi frame, hay một lần lúc bắt đầu | njin::ecs_register() với một njin::sys_phase | @ref game_loop |
| Vật lý chạy đều, không phụ thuộc FPS | `phase_fixed_update` | @ref time |
| Tạo entity, gắn component | njin::world() rồi `registry.create()`, `emplace<>()` | @ref ecs |
| Báo tin giữa các system | njin::events() | @ref ecs |
| Tạo nhiều entity giống nhau (quái, đạn) | njin::prefab_register(), njin::prefab_spawn() | @ref prefabs |
| Chia game thành menu, màn chơi, game over | njin::scene_register(), njin::scene_set() | @ref scenes |
| Chuyển cảnh mờ dần | njin::scene_fade() | @ref scenes |

## Nhân vật và chuyển động

| Muốn | Dùng | Xem |
|---|---|---|
| Nhân vật platformer chạy, nhảy | njin::platformer_body + njin::platformer_input_map | @ref first_jump, @ref platformer |
| Nhảy đôi, trượt tường, nhảy tường | `air_jumps`, `wall_slide_speed`, `wall_jump` của njin::platformer_body | @ref platformer |
| Nhân vật top-down đi 8 hướng, lướt | njin::topdown_body + njin::topdown_input_map | @ref first_walk, @ref topdown |
| Đẩy lùi khi trúng đòn | ghi thẳng vào `velocity` của body | @ref platformer |
| Bục di chuyển, lính gác đi tuần | njin::path_mover | @ref platformer |
| Quái đuổi theo, tìm đường tránh tường | njin::nav_grid_from_world(), njin::nav_find_path(), njin::nav_steer() | @ref topdown |
| Cho một vật chạy tới một điểm trong một khoảng thời gian | njin::tween_move() | @ref screen_timers |
| Tự viết bộ điều khiển di chuyển | njin::collision_move() | @ref collision |

## Va chạm

| Muốn | Dùng | Xem |
|---|---|---|
| Cho vật có hình va chạm | njin::collider (hộp hoặc tròn) | @ref collision |
| Biết hai vật vừa chạm nhau (đạn trúng quái, nhặt đồ) | event njin::collision_enter, njin::collision_exit, `trigger = true` | @ref collision |
| Chọn cái gì va chạm với cái gì | `layer` và `mask` của njin::collider, njin::layer_bit() | @ref collision |
| Tường và mặt đất từ bản đồ ô | collider `collider_tiles` trên entity của njin::tilemap | @ref collision, @ref tilemap |
| Có gì trong vùng đánh, vùng nổ, dưới con chuột | njin::collision_overlap_rect(), njin::collision_overlap_circle(), njin::collision_overlap_point() | @ref collision |
| Bắn tia, kiểm tra tầm nhìn | njin::collision_raycast(), njin::collision_line_of_sight() | @ref collision, @ref topdown |
| Nhìn thấy khung va chạm để debug | njin::collision_set_debug() | @ref collision |

## Bản đồ và màn chơi

| Muốn | Dùng | Xem |
|---|---|---|
| Vẽ bản đồ ô vuông bằng code | njin::tilemap, njin::tilemap_set() | @ref tilemap |
| Viết bản đồ bằng chữ, mỗi ký tự một ô (không cần Tiled hay LDtk) | njin::tilemap_from_text(), njin::tilemap_from_rows() | @ref tilemap |
| Ô là dốc, bục một chiều, không va chạm | njin::tilemap_set_shape() | @ref platformer |
| Nước, đuốc chuyển động | njin::tilemap_animate() | @ref tilemap |
| Tự sinh bản đồ top-down (đảo, vùng đất) từ nhiễu | njin::generate_topdown(), njin::noise_2d() | @ref procgen |
| Tự sinh màn platformer chơi được (hố, hang, bục) | njin::generate_platformer() | @ref procgen |
| Làm bản đồ sinh ra bớt vụn: xoá chấm lẻ, thêm viền, rải hoa | njin::grid_majority(), njin::grid_border(), njin::grid_scatter() | @ref procgen |
| Góc bo tròn tự nhiên cho đất, nước, tường (autotile) | njin::grid_autotile(), njin::autotile_index() | @ref procgen_autotile |
| Sinh bản đồ bằng Wave Function Collapse, từ mẫu hoặc luật tự viết | njin::wfc_learn(), njin::wfc_generate() | @ref procgen |
| Đưa lưới sinh ra vào tilemap | njin::tilemap_from_grid() | @ref procgen |
| Nạp màn chơi vẽ trong Tiled hoặc LDtk | njin::level_load(), njin::level_load_ldtk() | @ref level |
| Tìm điểm xuất hiện, cửa, quái đặt trong màn | njin::level_find() | @ref level |
| Xem ô nào ở vị trí nào, có chạm ô không | njin::tilemap_cell_at(), njin::tilemap_overlaps(), njin::tilemap_move() | @ref tilemap |

## Camera

| Muốn | Dùng | Xem |
|---|---|---|
| Tạo camera | njin::camera_spawn() | @ref camera |
| Camera bám nhân vật, không lộ ra ngoài màn | njin::camera_follow, `bounds` từ njin::level_bounds() | @ref camera, @ref platformer |
| Đổi vị trí chuột thành vị trí trong thế giới | njin::scr2w() (ngược lại njin::w2scr()) | @ref camera |
| Bỏ qua vẽ những gì ngoài màn hình | njin::camera_bounds() | @ref camera |
| Rung màn hình | njin::camera_shake() | @ref particles |
| Pixel art với độ phân giải cố định | njin::window_set_virtual_size(), hoặc `virtual_size` trong njin::njin_cfg | @ref drawing, @ref screen_timers |

## Nhập liệu

| Muốn | Dùng | Xem |
|---|---|---|
| Hỏi "nút nhảy có bấm không" (không hỏi phím cụ thể) | njin::action_define(), njin::action_pressed() | @ref input |
| Trục ngang/dọc từ hai phím hoặc cần analog | njin::axis_define(), njin::axis_value() | @ref input |
| Đọc thẳng phím, chuột, tay cầm | njin::key_pressed(), njin::mouse_pos(), njin::pad_axis() | @ref input |
| Gõ chữ | njin::text_char() | @ref input |
| Cho người chơi đổi phím | njin::action_rebind(), njin::input_bindings_save() | @ref settings |
| Rung tay cầm | njin::pad_rumble() | @ref settings |

## Vẽ

| Muốn | Dùng | Xem |
|---|---|---|
| Vẽ hình chữ nhật, tròn, đường thẳng | njin::draw_rect(), njin::draw_circle(), njin::draw_line() | @ref drawing |
| Vẽ chữ (kể cả tiếng Việt) | njin::draw_text(), njin::font_load() | @ref drawing |
| Đo hay ngắt dòng chữ | njin::text_measure(), njin::text_wrap() | @ref drawing |
| Nạp và vẽ ảnh | njin::texture_load(), njin::texture_draw() | @ref rendering |
| Ảnh pixel art không bị mờ | njin::texture_set_filter() với `filter_nearest` | @ref rendering |
| Nhân vật có ảnh | njin::sprite | @ref sprites |
| Sắp xếp ai đứng trước ai (top-down) | njin::draw_set_y_sort() | @ref topdown |
| Ghép nhiều ảnh nhỏ vào một trang | njin::atlas_create(), njin::atlas_load() | @ref rendering |
| Shader riêng | njin::shader_load(), njin::shader_set_f32() | @ref rendering |
| Thanh máu, vòng hồi chiêu, bóng đổ vẽ bằng công thức (SDF) | njin::shader_begin() quanh một ảnh kéo giãn, njin::shader_set_vec2() | @ref learn_shader_sdf |
| Vẽ vào một ảnh ngoài màn hình | njin::render_texture_load(), njin::render_texture_begin() | @ref rendering |

## Sprite và animation

| Muốn | Dùng | Xem |
|---|---|---|
| Animation từ một sprite sheet đều ô | njin::sprite_anim | @ref sprites |
| Animation từ Aseprite | njin::anim_sheet_load() | @ref animation |
| Chuyển idle, run, jump theo trạng thái | njin::anim_graph_create(), njin::animator_set_bool(), njin::animator_play() | @ref animation |

## Hiệu ứng và cảm giác chơi

| Muốn | Dùng | Xem |
|---|---|---|
| Nổ, bụi, tia lửa, khói, lửa | njin::particles_spawn() với mẫu trong `njin::fx::` | @ref particles |
| Dừng hình một chút khi trúng đòn | njin::hitstop() | @ref particles |
| Nháy trắng một sprite, nháy cả màn hình | njin::sprite_flash(), njin::screen_flash() | @ref particles |
| Kẻ địch chết: sprite tan biến (hoặc hiện ra dần) | njin::sprite_dissolve(), njin::dissolve_fx | @ref particles |
| Slow motion, tạm dừng | njin::time_set_scale(), njin::time_set_paused() | @ref time |
| Làm mờ, CRT, bloom, vignette toàn màn | njin::post_fx_set() | @ref post_processing |
| Chuyển mượt giữa hai bộ hiệu ứng | njin::post_fx_lerp() | @ref post_processing |

## Thời gian và ngẫu nhiên

| Muốn | Dùng | Xem |
|---|---|---|
| Thời gian của frame để nhân vào vận tốc | njin::delta() | @ref time |
| Làm gì đó sau N giây | njin::timer_after() | @ref screen_timers |
| Làm gì đó cứ mỗi N giây (sinh quái, hồi máu) | njin::timer_every() | @ref screen_timers |
| Chạy một giá trị từ A đến B, mượt | njin::tween_value(), njin::tween_move() | @ref screen_timers |
| Số ngẫu nhiên | njin::random() | @ref math |
| Hình học: vec2, hình chữ nhật, va chạm cơ bản | njin::vec2, njin::rect, njin::collide_rects() | @ref math |

## Âm thanh

| Muốn | Dùng | Xem |
|---|---|---|
| Phát tiếng động | njin::sound_load(), njin::sound_play_once() | @ref audio |
| Tiếng động theo vị trí, nhỏ dần khi ở xa | njin::sound_play_at() | @ref audio |
| Nhạc nền, chuyển nhạc mượt | njin::music_load(), njin::music_play(), njin::music_crossfade() | @ref audio |
| Thanh âm lượng nhạc, hiệu ứng, giao diện | njin::audio_set_bus_volume() | @ref settings |

## Giao diện, hộp thoại, đa ngôn ngữ

| Muốn | Dùng | Xem |
|---|---|---|
| Menu có nút, thanh trượt, dùng được với tay cầm | njin::ui_begin(), njin::ui_button(), njin::ui_slider() | @ref ui |
| Thông báo nhỏ "Đã lưu game" | njin::ui_toast() | @ref ui |
| Hỏi xác nhận (popup) | njin::ui_popup() | @ref ui |
| Hộp thoại NPC, chữ chạy, lựa chọn | njin::dialog_load(), njin::dialog_start() | @ref dialog |
| Nhiều ngôn ngữ | njin::i18n_load(), njin::i18n_set_language(), njin::tr() | @ref dialog |

## Lưu game, cài đặt, cửa sổ

| Muốn | Dùng | Xem |
|---|---|---|
| Đường dẫn lưu game của người dùng | njin::save_path() | @ref window_files |
| Đọc và ghi file | njin::file_read(), njin::file_write() | @ref window_files |
| Lưu game dạng JSON | njin::json_save(), njin::json_load() | @ref json |
| Lưu và nạp âm lượng, phím đã đổi | njin::settings_save(), njin::settings_load() | @ref settings |
| Toàn màn hình | njin::window_set_fullscreen() | @ref window_files |
| Chụp màn hình | njin::screenshot() | @ref window_files |

## Debug

| Muốn | Dùng | Xem |
|---|---|---|
| Xem FPS, entity, collider, log trong cửa sổ riêng | njin::debug_server_start() rồi mở njin_inspector | @ref debug |
| Theo dõi một giá trị đang đổi | njin::debug_watch() | @ref debug |
| Quay cửa sổ game thành GIF (bấm F9 trong inspector) | njin::debug_server_start() rồi mở njin_inspector | @ref debug_recording |
| Hiện component tự định nghĩa trong inspector | njin::debug_component | @ref debug |
| Ghi log | `NJIN_INFO`, `NJIN_WARN`, `NJIN_ERROR` | @ref logging |
| Sửa ảnh và shader khi game đang chạy | njin::hot_reload_enable() | @ref rendering |
