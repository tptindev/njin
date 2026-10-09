#pragma once
#include "_types.h"
#include "njin_3d.h"
#include <functional>

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

/// Một chân cho foot3d: đùi, cẳng chân, bàn chân (model_bone_find()). Gốc của
/// bàn chân là cổ chân.
struct foot3d_leg {
  i32 upper = -1; ///< Đùi.
  i32 lower = -1; ///< Cẳng chân.
  i32 foot = -1;  ///< Bàn chân.
};

/// Cách tạo một bộ đặt chân. Mảng `legs` chỉ cần sống đến khi foot3d_create()
/// trả về.
struct foot3d_desc {
  model_handle model{}; ///< Model có xương, đứng ở gốc của nó (lòng bàn chân ở y = 0).
  /// Các chân. nullptr: tự tìm hai chân người theo bone_humanoid_name()
  /// (`left_upper_leg`, `left_lower_leg`, `left_foot` và bên phải).
  const foot3d_leg *legs = nullptr;
  u32 leg_count = 0; ///< Số chân trong `legs`.
  /// Chân với lên hay xuống tối đa bấy nhiêu so với gốc của lần vẽ, đơn vị thế
  /// giới. Cao hơn bậc thang cao nhất mà nhân vật bước được.
  f32 max_step = 0.5f;
  /// Mặt nghiêng hơn bấy nhiêu độ thì bàn chân không nghiêng theo (vách, mép bậc).
  f32 max_tilt = 35.0f;
  /// Độ nhanh bám theo mặt đất mới, mỗi giây. Lớn: bám ngay; nhỏ: mượt hơn. Chân
  /// đi lên luôn nhanh gấp đôi, để không lún vào bậc vừa bước lên.
  f32 smoothing = 15.0f;
  /// Hướng đầu gối gập về phía đó, theo trục của model (thường là hướng nhìn).
  /// Dùng khi animation để chân thẳng; chân đã gập thì giữ phía gập của animation.
  vec3 knee_forward{0.0f, 0.0f, 1.0f};
};

/// Tạo một bộ đặt chân cho một nhân vật: mỗi frame, mỗi bàn chân của animation
/// được đặt lên mặt đất ngay dưới nó (bậc thang, dốc, đá), hông hạ xuống cho bàn
/// chân thấp hơn với tới, đầu gối gập bằng IK hai xương, lòng bàn chân nghiêng
/// theo mặt đất. Bàn chân đang nhấc trong animation (bước đi) vẫn nhấc, cao hơn
/// mặt đất dưới nó bấy nhiêu. Mỗi nhân vật một bộ (nó nhớ độ cao để làm mượt).
/// @param ctx Context của engine.
/// @param desc Model và các chân.
/// @return Handle; không hợp lệ nếu model không có xương hay không chân nào đủ ba
/// xương (cảnh báo nói vì sao). Giải phóng bằng foot3d_destroy().
foot3d_handle foot3d_create(context &ctx, const foot3d_desc &desc);

/// Mặt đất dưới một điểm, cho foot3d_update(): nhận điểm bắt đầu dò (thế giới)
/// và quãng dò xuống; trả `true` và ghi `point` (điểm chạm) và `normal` (pháp tuyến)
/// nếu có đất. Ví dụ trên địa hình: `point = {p.x, terrain3d_height(ctx, t, p.x, p.z), p.z}`.
using foot3d_ground = std::function<bool(vec3 from, f32 distance, vec3 &point, vec3 &normal)>;

/// Đặt chân lên mặt đất và ghi tư thế cuối vào `out`, dò đất bằng
/// physics3d_raycast() thẳng xuống dưới mỗi cổ chân (trúng body tĩnh, động,
/// kinematic và height field của địa hình; không trúng nhân vật character3d).
///
/// Thứ tự với các bước khác: retarget3d_pose() trước (nếu có), rồi foot3d_update()
/// với `pose.bones` là kết quả đó, rồi spring3d_update() với `pose.bones` là kết quả
/// của foot3d, rồi vẽ. Gốc của lần vẽ (`transform`) là chỗ nhân vật đứng (đáy
/// capsule của character3d).
///
/// @code
/// njin::bone_pose3d bones[64];
/// njin::foot3d_update(ctx, feet, {.anim = idle, .time = t}, at, dt, bones, 64);
/// njin::draw_model_anim(ctx, hero, at, {.bones = bones});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Bộ đặt chân.
/// @param pose Tư thế trước khi đặt chân (animation, hay xương đã tính).
/// @param transform Vị trí, hướng và tỉ lệ của lần vẽ (tỉ lệ nên đều ba trục).
/// @param dt Thời gian trôi qua, giây, để làm mượt. 0: đặt ngay, không làm mượt.
/// @param out Mảng nhận model_bone_count() xương, như model_bone_pose().
/// @param count Số phần tử của `out`.
/// @param weight Mức áp, 0..1: 0 là đúng animation, 1 là chân đặt hẳn lên đất
/// (giảm dần khi nhân vật nhảy lên).
/// @return Số xương đã ghi; 0 nếu handle không hợp lệ hay `count` nhỏ hơn số xương.
i32 foot3d_update(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                  bone_pose3d *out, i32 count, f32 weight = 1.0f);

/// Như bản trên, nhưng mặt đất do game cho bằng `ground` (địa hình, lưới ô, bất
/// kỳ hình gì không có trong physics3d).
/// @param ctx Context của engine.
/// @param handle Bộ đặt chân.
/// @param pose Tư thế trước khi đặt chân.
/// @param transform Vị trí, hướng và tỉ lệ của lần vẽ.
/// @param dt Thời gian trôi qua, giây.
/// @param out Mảng nhận model_bone_count() xương.
/// @param count Số phần tử của `out`.
/// @param ground Hàm dò mặt đất.
/// @param weight Mức áp, 0..1.
/// @return Số xương đã ghi.
i32 foot3d_update(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                  bone_pose3d *out, i32 count, const foot3d_ground &ground, f32 weight = 1.0f);

/// Hông đang hạ bao nhiêu (đơn vị thế giới, âm là hạ xuống) sau lần
/// foot3d_update() cuối, để camera hay vật cầm tay đi theo.
/// @param ctx Context của engine. @param handle Bộ đặt chân. @return Độ dời.
f32 foot3d_hip_offset(const context &ctx, foot3d_handle handle);

/// Đặt lại: lần foot3d_update() sau đặt chân ngay, không làm mượt từ chỗ cũ. Gọi
/// khi dịch chuyển tức thời nhân vật.
/// @param ctx Context của engine. @param handle Bộ đặt chân.
void foot3d_reset(context &ctx, foot3d_handle handle);

/// Hủy bộ đặt chân. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine. @param handle Bộ đặt chân.
void foot3d_destroy(context &ctx, foot3d_handle handle);
/// @}
} // namespace njin
