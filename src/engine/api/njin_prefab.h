#pragma once
#include "_comps.h"
#include "_types.h"
#include <entt/entity/fwd.hpp>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_prefab
/// @{

/// Hàm dựng một prefab: gắn component vào entity vừa tạo.
///
/// Entity đã có sẵn transform (và njin::scene_owned nếu prefab yêu cầu) khi
/// hàm được gọi. Hàm có thể spawn thêm prefab khác và gắn chúng làm con bằng
/// njin::child_of, ví dụ nhân vật kèm vũ khí.
using prefab_fnc = void (*)(njin_ctx &ctx, entt::entity entity);

/// Mô tả một prefab, dùng với prefab_register().
///
/// Prefab là **mẫu để tạo entity**: một hàm dựng có tên. Viết hàm dựng một
/// lần, rồi spawn bao nhiêu lần cũng được ở bất cứ đâu, kể cả tìm theo tên
/// (ví dụ tên đọc từ file màn chơi).
struct prefab_desc {
  const char *name = nullptr; ///< Tên prefab, phải là duy nhất.
  prefab_fnc build = nullptr; ///< Hàm dựng. Không được null.
  /// Gắn entity vào scene đang chạy lúc spawn (njin::scene_owned), để nó tự
  /// mất khi rời scene. Tắt cho thứ sống qua nhiều scene.
  bool scene_owned = true;
};

/// Đăng ký một prefab. Nếu tên đã có thì trả về prefab cũ và bỏ qua `desc`.
/// @param ctx Context của engine.
/// @param desc Mô tả prefab.
/// @return Handle của prefab, hoặc handle id 0 nếu thiếu tên hoặc hàm dựng.
prefab_handle prefab_register(njin_ctx &ctx, const prefab_desc &desc);

/// Tìm prefab theo tên.
/// @param ctx Context của engine.
/// @param name Tên prefab.
/// @return Handle của prefab, hoặc handle id 0 nếu không có.
prefab_handle prefab_find(const njin_ctx &ctx, const char *name);

/// Tạo một entity từ prefab, đặt tại `at`.
///
/// Thứ tự: tạo entity, gắn transform `at`, gắn njin::scene_owned (nếu
/// `prefab_desc::scene_owned` và đang có scene), rồi gọi hàm dựng. Sửa gì
/// thêm thì sửa trên entity trả về.
/// @code
/// const entt::entity e = njin::prefab_spawn(ctx, g.coin, {.pos = {120, 80}});
/// world(ctx).get<coin>(e).value = 5;
/// @endcode
/// @param ctx Context của engine.
/// @param prefab Prefab cần tạo.
/// @param at Transform ban đầu.
/// @return Entity vừa tạo, hoặc `entt::null` nếu handle không hợp lệ.
entt::entity prefab_spawn(njin_ctx &ctx, prefab_handle prefab,
                          const transform &at = {});

/// Như prefab_spawn(), tìm prefab theo tên.
/// @param ctx Context của engine.
/// @param name Tên prefab.
/// @param at Transform ban đầu.
/// @return Entity vừa tạo, hoặc `entt::null` nếu không có prefab tên đó.
entt::entity prefab_spawn(njin_ctx &ctx, const char *name,
                          const transform &at = {});

/// Tạo một entity từ prefab và gắn nó làm con của `parent`.
///
/// `local` là vị trí so với cha (xem njin::child_of). Transform của entity
/// được tính ngay, nên đúng vị trí từ frame đầu tiên.
/// @param ctx Context của engine.
/// @param prefab Prefab cần tạo.
/// @param parent Entity cha. Phải có transform.
/// @param local Transform so với cha.
/// @return Entity vừa tạo, hoặc `entt::null` nếu handle không hợp lệ.
entt::entity prefab_spawn_child(njin_ctx &ctx, prefab_handle prefab,
                                entt::entity parent,
                                const transform &local = {});
/// @}
} // namespace njin
