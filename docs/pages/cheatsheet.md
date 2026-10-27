# Muốn làm X thì dùng gì {#cheatsheet}

Bảng tra nhanh: tìm việc bạn muốn làm ở cột trái, dùng thứ ở cột giữa, đọc trang ở cột phải.
Mọi thứ nằm trong `njin::` và chỉ cần `#include <njin.h>`. Muốn xem đầy đủ tham số của một hàm thì bấm vào tên
nó, hoặc vào [Nhóm API](topics.html).

Chưa biết bắt đầu từ đâu? Đọc @ref first_jump (platformer) hoặc @ref first_walk (top-down) trước: một nhân
vật chạy được trong 50 dòng. Chưa quen `entt::registry`? Đọc @ref ecs.

## Khởi động và cấu trúc game

| Muốn | Dùng | Xem |
|---|---|---|
| Mở cửa sổ, chạy, đóng | njin::create(), njin::run(), njin::destroy() | @ref getting_started |
| Thoát game từ trong game | njin::quit() | @ref window_files |
| Gom logic của game vào một chỗ | njin::mod_desc với `setup`, đăng ký bằng njin::mod_register() | @ref modules_systems |
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
| Hàng trăm con cùng về một đích (tower defense, RTS, bầy quái) | njin::nav_flow_build(), njin::nav_flow_dir() | @ref topdown |
| Đặt công trình, cây trồng, đồ trong túi lên lưới, không chồng nhau | njin::place_grid, njin::place_fits(), njin::place_put() | @ref topdown |
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
| Bản đồ isometric, so le, lục giác; ô dưới chuột | njin::tilemap::orientation, njin::tilemap_cell_at(), njin::tilemap_cell_polygon() | @ref tilemap |
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
| Pixel art với độ phân giải cố định | njin::window_set_virtual_size(), hoặc `virtual_size` trong njin::config | @ref drawing, @ref screen_timers |

## Nhập liệu

| Muốn | Dùng | Xem |
|---|---|---|
| Hỏi "nút nhảy có bấm không" (không hỏi phím cụ thể) | njin::action_define(), njin::action_pressed() | @ref input |
| Trục ngang/dọc từ hai phím hoặc cần analog | njin::axis_define(), njin::axis_value() | @ref input |
| Đọc thẳng phím, chuột, tay cầm | njin::key_pressed(), njin::mouse_pos(), njin::pad_axis() | @ref input |
| Màn hình cảm ứng: nhiều ngón, chạm, giữ, kéo, dang hai ngón, xoay | njin::touch_get(), njin::gestures() | @ref input |
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
| Ánh sáng: đèn điểm, đèn nón, đèn hướng, ban đêm, mặt trời | njin::lighting_set(), njin::light_2d | @ref lighting |
| Bóng đổ đúng từng pixel của sprite (cây, bụi, đá, nhân vật) | njin::light_occluder_pixels | @ref lighting |
| Bóng đổ từ tường, hàng rào, hình hình học | njin::light_occluder, njin::light_occluder_capsule(), njin::light_occluder_sprite | @ref lighting |
| Bóng đổ từ tường của tilemap | njin::light_occluders_from_tiles() | @ref lighting |
| Sprite nổi khối, kim loại, bóng gương dưới đèn | `sprite::normal`, `sprite::material` (PBR, xếp kênh MRA như raylib) | @ref lighting |
| Sprite tự phát sáng (hoa dạ quang, mắt quái, cửa sổ) | `sprite::emissive`, `sprite::emissive_power` | @ref lighting |
| Đổi cách nén HDR (Reinhard, ACES) | njin::lighting_desc::tonemap | @ref lighting |
| Shader riêng | njin::shader_load(), njin::shader_set_f32() | @ref rendering |
| Shader đọc thêm một ảnh (bảng màu, nhiễu, mặt nạ) | njin::shader_set_texture() | @ref shader_advanced |
| Shader nhận một danh sách (các nguồn sáng) | njin::shader_set_vec4_array() | @ref shader_advanced |
| Một shader chạy trên cả khung hình: đêm có đèn, hoàng hôn, sương mù | njin::camera_set_post_shader() kèm ảnh phụ và mảng uniform | @ref shader_advanced |
| Vẽ hàng nghìn hình giống nhau bằng một lệnh vẽ | njin::instance_buffer_create(), njin::draw_instanced() | @ref instancing |
| Lưu render texture (sprite sheet đã bake) ra PNG | njin::render_texture_save() | @ref instancing_bake |
| Vẽ trước các hình vào một sprite sheet, đọc lại mỗi frame | njin::render_texture_begin(), njin::draw_instanced() kèm render texture, njin::render_texture_set_filter() | @ref instancing_bake |
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
| Icon, ảnh HUD tan biến hoặc hiện ra dần từng mảng | njin::draw_dissolve_begin(), njin::draw_dissolve_end() | @ref particles |
| Slow motion, tạm dừng | njin::time_set_scale(), njin::time_set_paused() | @ref time |
| Làm mờ, CRT, bloom, vignette toàn màn | njin::post_fx_set() | @ref post_processing |
| Chuyển mượt giữa hai bộ hiệu ứng | njin::post_fx_lerp() | @ref post_processing |

## 3D

Cần bật feature trong CMake: `njin_use(<game> FEATURES 3d)`, thêm `physics3d` cho vật lý, `nav3d` cho tìm đường.

| Muốn | Dùng | Xem |
|---|---|---|
| Vẽ 3D: camera, hình khối, màu | njin::begin_3d(), njin::draw_cube3d(), njin::draw_sphere3d(), njin::end_3d() | @ref graphics_3d |
| Camera nhìn song song (isometric, chiến thuật) | njin::camera3d::projection, njin::camera3d::ortho_height | @ref graphics_3d |
| Hình mịn bằng SDF, khối dẻo nối nhau | njin::draw_shape3d(), njin::draw_sdf_blend() | @ref graphics_3d |
| Nạp và vẽ model glTF, cỡ thật của model | njin::model_load(), njin::draw_model(), njin::model_bounds() | @ref graphics_3d |
| Đổi vật liệu, màu của một phần model | njin::model_material_get(), njin::model_material_set() | @ref graphics_3d |
| Animation xương, trộn hai animation | njin::draw_model_anim(), njin::model_anim_find() | @ref graphics_3d |
| Tóc, váy, đuôi đung đưa theo chuyển động | njin::spring3d_create(), njin::spring3d_update() | @ref spring3d |
| Chân đặt đúng mặt dốc, bậc thang | njin::foot3d_create(), njin::foot3d_update() | @ref foot3d |
| Dùng animation của bộ xương khác | njin::retarget3d_create(), njin::retarget3d_pose() | @ref retarget3d |
| Ít đa giác khi ở xa | njin::model_lod_build() | @ref model_lod |
| Bỏ vật bị che, gom lệnh vẽ, sắp xếp vật đục | njin::render3d_options_set() | @ref render3d_options |
| Mặt trời, ánh sáng nền, bóng đổ, sương | njin::light3d_set() | @ref graphics_3d |
| Đèn điểm, đèn nón, đèn pin có bóng | njin::light3d_add() | @ref graphics_3d |
| Ánh sáng mạnh không cháy trắng (tone mapping) | njin::light3d::tonemap, njin::light3d::exposure | @ref pbr3d |
| Vật liệu PBR: kim loại, độ nhám, model glTF như Blender | njin::material3d::pbr, njin::model_load_desc::pbr | @ref pbr3d |
| Ánh sáng và phản chiếu từ ảnh HDR, nền trời bằng ảnh | njin::light3d::environment | @ref pbr3d |
| Vẽ HDR, bloom theo năng lượng, tự chỉnh độ phơi sáng | njin::light3d::hdr, njin::light3d::auto_exposure, njin::post3d::bloom | @ref hdr3d |
| Sơn bóng, kim loại xước, kính khúc xạ, parallax, lightmap | njin::material3d::clearcoat, njin::material3d::transmission, njin::material3d::height | @ref pbr3d_more |
| Phản chiếu trong phòng, ánh sáng hắt từ tường | njin::probe3d_add(), njin::irradiance3d_set() | @ref probe3d |
| Bóng sắc gần, vẫn có bóng ở xa | njin::light3d::shadow_cascades | @ref shadow3d_cascades |
| Hàng trăm đèn, nhiều đèn đổ bóng | njin::light3d::lights_max, njin::light3d::source_shadows | @ref lights3d_many |
| Luồng nắng trong sương, đèn pin trong sương | njin::light3d::volume_density | @ref fog3d_volume |
| Bóng mềm bằng ray tracing | njin::light3d::ray_traced_shadows, njin::raytracing_available() | @ref shadow3d_raytraced |
| Phản chiếu bằng ray tracing | njin::light3d::ray_traced_reflections | @ref reflect3d_raytraced |
| Ánh sáng gián tiếp bằng ray tracing (GI) | njin::irradiance3d_desc::ray_traced | @ref probe3d |
| Nhiều lớp kính chồng nhau, khử răng cưa FXAA/SMAA | njin::post3d::oit, njin::post3d::aa | @ref post3d_oit |
| Nháy trắng khi trúng đòn, tan biến khi chết | njin::fx3d_set() | @ref graphics_3d |
| Lửa, khói, tia lửa 3D; phát liên tục từ vòng, đĩa | njin::particles3d_spawn(), njin::particles3d_emit() | @ref graphics_3d |
| Vẽ hàng nghìn bản của một model | njin::draw_instanced3d() | @ref graphics_3d |
| Chỉ vẽ instance camera thấy, cây xa ít tam giác | njin::draw_instanced3d() với njin::instanced3d_desc | @ref instancing3d_cull |
| Chọn vật bằng chuột, nhãn trên đầu nhân vật | njin::camera3d_ray(), njin::ray3d_model(), njin::camera3d_to_screen() | @ref graphics_3d |
| Model và hình 3D thành entity | njin::model3d, njin::shape3d_render | @ref entities_3d |
| Vật lý 3D: rơi, va, đẩy | njin::body3d_create(), njin::physics3d_raycast() | @ref physics3d |
| Nhân vật đi trên địa hình, cầu thang | njin::character3d_create(), njin::character3d_set_velocity() | @ref physics3d |
| Ragdoll, vải và vật mềm, xe | njin::body3d_create() | @ref physics3d |
| Tìm đường 3D, đám đông tránh nhau | njin::navmesh3d_create(), njin::nav3d_agent_add() | @ref nav_3d |
| Địa hình, cỏ, cây rải, nước | njin::terrain3d_create(), njin::grass3d_create(), njin::scatter3d_create(), njin::water3d_create() | @ref world_3d |
| Bầu trời theo giờ, thời tiết | njin::draw_sky3d(), njin::weather3d_preset() | @ref sky3d |
| Góc tối, phản chiếu sàn, vết đạn, chống răng cưa, mờ chuyển động | njin::post3d_set(), njin::decal3d_add() | @ref post_3d |
| Vẽ đường, mũi tên debug trong 3D | njin::gizmo_line3d(), njin::gizmo_arrow3d() | @ref graphics_3d |

## Thời gian và ngẫu nhiên

| Muốn | Dùng | Xem |
|---|---|---|
| Thời gian của frame để nhân vào vận tốc | njin::delta() | @ref time |
| Làm gì đó sau N giây | njin::timer_after() | @ref screen_timers |
| Làm gì đó cứ mỗi N giây (sinh quái, hồi máu) | njin::timer_every() | @ref screen_timers |
| Chạy một giá trị từ A đến B, mượt | njin::tween_value(), njin::tween_move() | @ref screen_timers |
| Số ngẫu nhiên | njin::random() | @ref math |
| Giờ thật của máy, kể cả lúc game tắt (xây trong 8 tiếng, thưởng mỗi ngày) | njin::time_unix(), njin::time_date(), njin::time_from_date() | @ref time |
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
