#pragma once
#include "_types.h"
#include "njin_3d.h"

namespace njin {
struct context;

/// @addtogroup grp_anim3d
/// @{

/// Một chuỗi xương lò xo: xương `bone` và mọi xương nằm dưới nó (tóc, đuôi,
/// râu, vạt áo). Mỗi xương trễ theo quán tính khi nhân vật chuyển động, đung
/// đưa, rồi trở về tư thế của animation. Các tham số giống spring bone của VRM,
/// nên số trong một file VRM dùng được như cũ.
struct spring3d_chain {
  i32 bone = -1;          ///< Xương gốc của chuỗi (model_bone_find()).
  /// Lực kéo về tư thế của animation. Lớn: cứng, ít đung đưa. 0.5 đến 4 là thường.
  f32 stiffness = 1.0f;
  /// Lực cản, 0..1: 0 đung đưa mãi, 1 không đung đưa (chỉ trễ rồi về).
  f32 drag = 0.4f;
  f32 gravity = 0.0f;                     ///< Trọng lực kéo đầu xương, đơn vị mỗi giây.
  vec3 gravity_dir{0.0f, -1.0f, 0.0f};    ///< Hướng trọng lực trong thế giới, độ dài 1.
  f32 radius = 0.02f;                     ///< Bán kính va chạm của đầu mỗi xương, đơn vị của model.
};

/// Một collider mà xương lò xo không xuyên qua (đầu, thân, vai), gắn vào một
/// xương nên đi theo animation. Hình cầu khi `tail` bằng `offset`, hình viên
/// thuốc (capsule) từ `offset` đến `tail` khi khác.
struct spring3d_collider {
  i32 bone = -1;          ///< Xương mang collider; -1 là gốc của model.
  vec3 offset{};          ///< Tâm, theo ba trục của xương tính từ gốc của nó (đơn vị của model).
  vec3 tail{};            ///< Đầu kia của capsule, cùng hệ trục với `offset`.
  f32 radius = 0.1f;      ///< Bán kính, đơn vị của model.
};

/// Cách tạo một bộ xương lò xo. Các mảng chỉ cần sống đến khi spring3d_create()
/// trả về.
struct spring3d_desc {
  model_handle model{};                          ///< Model có xương.
  const spring3d_chain *chains = nullptr;        ///< Các chuỗi. Một xương ở hai chuỗi thuộc chuỗi đầu.
  u32 chain_count = 0;                           ///< Số chuỗi.
  const spring3d_collider *colliders = nullptr;  ///< Các collider, dùng chung cho mọi chuỗi.
  u32 collider_count = 0;                        ///< Số collider.
};

/// Tạo một bộ xương lò xo cho một nhân vật. Mỗi nhân vật cần một bộ riêng (nó
/// nhớ đầu các xương đang ở đâu); nhiều nhân vật dùng chung một model thì mỗi
/// con một bộ.
///
/// @code
/// const njin::spring3d_chain hair{.bone = njin::model_bone_find(ctx, girl, "hair_1"), .stiffness = 1.5f};
/// const njin::spring3d_collider head{.bone = njin::model_bone_find(ctx, girl, "head"), .offset = {0, 0.1f, 0},
///                                    .tail = {0, 0.1f, 0}, .radius = 0.11f};
/// njin::spring3d_handle springs = njin::spring3d_create(
///     ctx, {.model = girl, .chains = &hair, .chain_count = 1, .colliders = &head, .collider_count = 1});
/// @endcode
/// @param ctx Context của engine.
/// @param desc Model, các chuỗi và collider.
/// @return Handle; không hợp lệ nếu model không có xương hay không chuỗi nào có
/// xương hợp lệ (cảnh báo nói vì sao). Giải phóng bằng spring3d_destroy().
spring3d_handle spring3d_create(context &ctx, const spring3d_desc &desc);

/// Chạy lò xo `dt` giây và ghi tư thế cuối của mọi xương vào `out`: tư thế
/// `pose` (animation, hay xương game đặt), với các xương lò xo đã trễ và đung
/// đưa. Mô phỏng trong thế giới, theo `transform` của lần vẽ, nên nhân vật chạy
/// thì tóc bay ra sau. Truyền `out` cho `model_pose::bones` để vẽ.
///
/// Gọi mỗi frame một lần cho mỗi nhân vật, trước khi vẽ. Lò xo chạy theo bước
/// 1/60 giây (nhiều bước khi `dt` lớn, `dt` trên 0.1 giây bị cắt); `dt` 0 (game
/// tạm dừng) không mô phỏng, chỉ đặt xương theo đầu xương hiện tại.
///
/// @code
/// njin::bone_pose3d bones[64];
/// const njin::model_pose walk{.anim = walk_anim, .time = t};
/// njin::spring3d_update(ctx, springs, walk, at, dt, bones, 64);
/// njin::draw_model_anim(ctx, girl, at, {.anim = walk_anim, .time = t, .bones = bones});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Bộ lò xo.
/// @param pose Tư thế trước lò xo.
/// @param transform Vị trí, hướng và tỉ lệ của lần vẽ (tỉ lệ nên đều ba trục).
/// @param dt Thời gian trôi qua, giây (thường là delta()).
/// @param out Mảng nhận model_bone_count() xương, trong không gian của model như model_bone_pose().
/// @param count Số phần tử của `out`.
/// @return Số xương đã ghi; 0 nếu handle không hợp lệ hay `count` nhỏ hơn số xương.
i32 spring3d_update(context &ctx, spring3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                    bone_pose3d *out, i32 count);

/// Đặt lại: lần spring3d_update() sau, mọi xương lò xo bắt đầu đứng yên ở tư
/// thế của animation. Gọi khi dịch chuyển tức thời nhân vật (hồi sinh, qua cổng),
/// để tóc không bay theo cả quãng đường.
/// @param ctx Context của engine.
/// @param handle Bộ lò xo.
void spring3d_reset(context &ctx, spring3d_handle handle);

/// Hủy bộ lò xo. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Bộ lò xo.
void spring3d_destroy(context &ctx, spring3d_handle handle);

/// Tên chuẩn của một xương người (humanoid) từ tên của nó trong file: hiểu cách
/// đặt tên của Mixamo (`mixamorig:LeftForeArm`), Unreal (`lowerarm_l`,
/// `spine_02`), Unity và VRM (`LeftLowerArm`, `UpperChest`) và Blender
/// (`forearm.L`, `thigh.R`). Kết quả là một trong: `hips`, `spine`, `chest`,
/// `upper_chest`, `neck`, `head`, và theo hai bên (`left_`, `right_`):
/// `shoulder`, `upper_arm`, `lower_arm`, `hand`, `upper_leg`, `lower_leg`,
/// `foot`, `toes`, `thumb_1`..`thumb_3`, `index_1`..`index_3`, `middle_1`..,
/// `ring_1`.., `little_1`.. (đốt ngón tính từ bàn tay).
/// @param name Tên xương.
/// @return Tên chuẩn (chuỗi tĩnh), hoặc chuỗi rỗng nếu không nhận ra.
const char *bone_humanoid_name(const char *name);

/// Cách chép animation của model `source` sang model `target` có bộ xương
/// khác: tên khác, tay chân dài ngắn khác.
struct retarget3d_desc {
  model_handle source{};  ///< Model có các animation (bộ xương nguồn).
  model_handle target{};  ///< Model được vẽ (bộ xương đích).
  /// Cặp tên xương do game ghép: `pairs[2 * i]` của nguồn đi với `pairs[2 * i + 1]`
  /// của đích. Được ưu tiên trước cách ghép tự động.
  const char *const *pairs = nullptr;
  u32 pair_count = 0;     ///< Số cặp.
  /// Ghép tự động các xương còn lại: cùng tên chuẩn bone_humanoid_name(), hoặc
  /// cùng tên (bỏ tiền tố như `mixamorig:`, không phân biệt hoa thường).
  bool humanoid = true;
};

/// Chuẩn bị chép animation từ `desc.source` sang `desc.target`.
///
/// Mỗi xương được ghép xoay **so với tư thế gốc**: xương đích xoay đúng góc mà
/// xương nguồn xoay khỏi tư thế gốc của nó, trong không gian của model. Nên hai
/// model cần cùng trục (cùng hướng lên, cùng hướng nhìn) và tư thế gốc giống
/// nhau (cùng là chữ T hay cùng chữ A); nếu không, tay sẽ lệch đúng bằng độ lệch
/// của hai tư thế gốc. Xương đích không được ghép đi theo xương cha của nó như
/// ở tư thế gốc. Hông (`hips`) còn được dời theo hông nguồn, nhân với tỉ lệ độ
/// cao hông (chiều dài chân) của hai model, nên người lùn bước ngắn hơn và chân
/// không trượt.
/// @param ctx Context của engine.
/// @param desc Hai model và cách ghép xương.
/// @return Handle; không hợp lệ nếu một model không có xương hay không xương
/// nào ghép được (cảnh báo nói vì sao). Giải phóng bằng retarget3d_destroy().
retarget3d_handle retarget3d_create(context &ctx, const retarget3d_desc &desc);

/// Tư thế của model đích khi model nguồn ở tư thế `pose`. Truyền `out` cho
/// `model_pose::bones` khi vẽ model đích.
///
/// @code
/// njin::bone_pose3d bones[80];
/// njin::retarget3d_pose(ctx, to_dwarf, {.anim = run, .time = t}, bones, 80);
/// njin::draw_model_anim(ctx, dwarf, at, {.bones = bones});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Cách chép.
/// @param pose Tư thế của model nguồn (animation của nó, hay xương game đặt).
/// @param out Mảng nhận model_bone_count() xương của model đích, như model_bone_pose().
/// @param count Số phần tử của `out`.
/// @return Số xương đã ghi; 0 nếu handle không hợp lệ hay `count` nhỏ hơn số xương.
i32 retarget3d_pose(const context &ctx, retarget3d_handle handle, const model_pose &pose, bone_pose3d *out,
                    i32 count);

/// Xương nguồn được ghép với xương `bone` của model đích, để kiểm tra cách ghép.
/// @param ctx Context của engine.
/// @param handle Cách chép.
/// @param bone Xương của model đích, 0..model_bone_count() - 1.
/// @return Xương của model nguồn, hoặc -1 nếu `bone` không được ghép hay handle không hợp lệ.
i32 retarget3d_source_bone(const context &ctx, retarget3d_handle handle, i32 bone);

/// Hủy cách chép. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Cách chép.
void retarget3d_destroy(context &ctx, retarget3d_handle handle);
/// @}
} // namespace njin
