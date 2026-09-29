#pragma once
#include "_math.h"
#include "_types.h"
#include <memory>
#include <vector>

namespace njin {

/// @addtogroup grp_spatial
/// @{

/// Cấu trúc bên dưới một njin::spatial_index. Cả hai trả cùng kết quả, chỉ
/// khác tốc độ: đo trong game của bạn rồi chọn.
enum spatial_kind : u8 {
  /// Lưới đều. Dựng lại rất rẻ; nhanh nhất khi các vật cùng cỡ và rải khá đều
  /// trong một vùng có giới hạn (đám đông, đàn quái, đạn).
  spatial_grid,
  /// Quadtree: chia nhỏ chỗ đông, bỏ qua chỗ trống. Hợp khi vật dồn cục, map
  /// rộng mà thưa, hoặc truy vấn bán kính lớn.
  spatial_quadtree,
};

/// Một vật trong index: hình tròn tâm `pos`. Số thứ tự của nó trong mảng đưa
/// cho spatial_build() là tên của nó trong mọi kết quả.
struct spatial_item {
  vec2 pos{};        ///< Tâm, trong thế giới.
  f32 radius = 0.0f; ///< Bán kính. 0 là một điểm.
  /// Nhóm, 0 là không nhóm. Các vật cùng một nhóm khác 0 không đẩy nhau trong
  /// spatial_separate(), và spatial_query::group bỏ qua chúng.
  u32 group = 0;
  /// spatial_separate() không dời vật này; vật khác chồng lên nó thì tự lùi
  /// hết phần chồng: người đang ngồi, cột đèn, chuỗi người đang đi.
  bool fixed = false;
};

/// Cách dựng một njin::spatial_index.
struct spatial_desc {
  spatial_kind kind = spatial_grid; ///< Lưới hay quadtree.
  /// Vùng chứa các vật. Để rỗng (mặc định) thì lấy hình chữ nhật bao mọi vật.
  /// Vật nằm ngoài vùng vẫn được tìm thấy, chỉ chậm hơn.
  rect bounds{};
  /// Lưới: cỡ ô, đơn vị thế giới. 0 (mặc định) là hai lần bán kính lớn nhất,
  /// hợp với spatial_separate(). Lưới không bao giờ có quá 4 ô mỗi vật: ô nhỏ
  /// hơn thế bị nới ra.
  f32 cell_size = 0.0f;
  u32 leaf_size = 8;  ///< Quadtree: một lá có hơn chừng này vật thì chia bốn.
  u32 max_depth = 12; ///< Quadtree: độ sâu tối đa, để nhiều vật trùng chỗ không chia mãi.
};

struct spatial_impl;

/// Index không gian cho hàng nghìn hình tròn: tìm k vật gần nhất và đẩy các
/// vật chồng nhau ra, không cần collider, không sinh event.
///
/// Dựng lại mỗi frame bằng spatial_build() từ vị trí mới, rồi hỏi bao nhiêu lần
/// tùy ý. Index giữ bản sao của các vật, nên sửa mảng gốc sau khi dựng không
/// làm hỏng nó. Giữ index qua các frame để dùng lại bộ nhớ.
struct spatial_index {
  spatial_index();  ///< Index rỗng; dựng bằng spatial_build().
  ~spatial_index(); ///< Giải phóng bộ nhớ của index.
  spatial_index(spatial_index &&) noexcept;            ///< Chuyển index, không chép.
  /// Chuyển index, không chép.
  /// @return Chính index này.
  spatial_index &operator=(spatial_index &&) noexcept;
  std::unique_ptr<spatial_impl> impl; ///< Bên trong, không đụng tới.
};

/// Dựng (lại) index từ `count` vật.
/// @param index Index, dựng lại từ đầu.
/// @param desc Lưới hay quadtree và các thông số.
/// @param items Các vật. Vật thứ `i` có tên `i` trong kết quả.
/// @param count Số vật.
void spatial_build(spatial_index &index, const spatial_desc &desc, const spatial_item *items, u32 count);

/// Như bản trên, từ một vector.
/// @param index Index.
/// @param desc Cách dựng.
/// @param items Các vật.
inline void spatial_build(spatial_index &index, const spatial_desc &desc, const std::vector<spatial_item> &items) {
  spatial_build(index, desc, items.data(), (u32)items.size());
}

/// Số vật trong index.
/// @param index Index.
/// @return Số vật của lần dựng gần nhất, 0 nếu chưa dựng.
u32 spatial_size(const spatial_index &index);

/// Một câu hỏi cho spatial_nearest().
struct spatial_query {
  vec2 at{};         ///< Điểm hỏi.
  /// Bán kính tìm quanh `at`. Dùng `INFINITY` để không giới hạn (k vật gần
  /// nhất dù xa đến đâu).
  f32 radius = 0.0f;
  /// `false`: vật được tính khi **tâm** của nó cách `at` dưới `radius`.
  /// `true`: khi **hình tròn** của nó chồng lên hình tròn (`at`, `radius`), tức
  /// khoảng cách tâm dưới `radius + item.radius`: ai đang chạm vào tôi.
  bool touching = false;
  u32 skip = 0xFFFFFFFFu; ///< Tên một vật bỏ qua, thường là chính người hỏi.
  u32 group = 0;          ///< Khác 0: bỏ qua vật cùng nhóm này.
  /// Lọc thêm: trả về `false` để bỏ qua vật `item`. Chỉ gọi cho vật đã qua
  /// điều kiện khoảng cách, nên có thể đắt hơn một phép so. Có thể null.
  bool (*filter)(u32 item, void *user) = nullptr;
  void *user = nullptr; ///< Truyền nguyên cho `filter`.
};

/// Một vật tìm thấy.
struct spatial_hit {
  u32 item = 0;           ///< Tên của vật: số thứ tự trong mảng đã dựng.
  f32 distance_sq = 0.0f; ///< Bình phương khoảng cách từ `at` tới tâm của nó.
};

/// Tìm tối đa `k` vật gần `q.at` nhất thỏa `q`, gần nhất trước.
/// @code
/// njin::spatial_hit near[4];
/// const njin::u32 n = njin::spatial_nearest(index, {.at = pos, .radius = 120.0f, .skip = self}, near, 4);
/// @endcode
/// @param index Index đã dựng.
/// @param q Câu hỏi.
/// @param out Nhận kết quả, đủ chỗ cho `k`.
/// @param k Số vật tối đa.
/// @return Số vật tìm thấy, từ 0 đến `k`.
u32 spatial_nearest(const spatial_index &index, const spatial_query &q, spatial_hit *out, u32 k);

/// Như bản trên; `out` được xóa rồi nhận kết quả.
/// @param index Index đã dựng.
/// @param q Câu hỏi.
/// @param out Nhận kết quả, gần nhất trước.
/// @param k Số vật tối đa.
/// @return Số vật tìm thấy.
inline u32 spatial_nearest(const spatial_index &index, const spatial_query &q, std::vector<spatial_hit> &out, u32 k) {
  out.resize(k);
  const u32 n = spatial_nearest(index, q, out.data(), k);
  out.resize(n);
  return n;
}

/// Tính độ đẩy để các vật chồng nhau tách ra: va chạm mềm cho đám đông.
///
/// Mỗi vật không `fixed` bị đẩy bởi **k vật chồng lên nó gần nhất**: hai vật
/// đều di chuyển được thì mỗi bên lùi một nửa phần chồng, vật `fixed` thì bên
/// kia lùi hết. Vật cùng nhóm (khác 0) không đẩy nhau. Chỉ cộng lực của k vật
/// gần nhất, nên giữa một cụm dày một vật không bị hàng chục vật cùng hất ra;
/// phần chồng còn lại tan dần qua các frame. Độ đẩy tính từ vị trí lúc dựng,
/// nên không phụ thuộc thứ tự, và không quá bán kính của vật.
///
/// Gọi mỗi frame sau khi dựng lại index, rồi cộng `push[i]` vào vị trí vật `i`.
/// @param index Index đã dựng.
/// @param push Nhận độ đẩy của từng vật, cùng số thứ tự với mảng đã dựng (0 với
/// vật `fixed` và vật không chạm ai).
/// @param k Số vật chồng gần nhất được xét cho mỗi vật.
/// @return Tổng số lần đẩy (một cặp hai vật di chuyển được tính hai lần).
u32 spatial_separate(const spatial_index &index, std::vector<vec2> &push, u32 k = 6);
/// @}
} // namespace njin
