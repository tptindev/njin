#pragma once
#include "_math.h"
#include "_types.h"
#include <string>
#include <string_view>

namespace njin {

/// @addtogroup grp_bits
/// @{

/// Ghi dữ liệu nhị phân theo từng bit: gói mạng, replay, save nhỏ gọn.
///
/// Mỗi giá trị chỉ chiếm số bit nó cần: một `bool` là 1 bit, một số trong 0..100
/// là 7 bit, một tọa độ trong -500..500 m chính xác tới 1 cm là 17 bit thay vì 32.
/// Đọc lại bằng bit_reader, **đúng thứ tự và đúng tham số** đã ghi.
/// @code
/// njin::bit_writer w;
/// w.write_int(player_id, 0, 63);
/// w.write_vec3(position, -500.0f, 500.0f, 0.01f);
/// w.write_bool(jumping);
/// njin::net_peer_send(ctx, server, w.data(), njin::net_delivery::unreliable);
/// @endcode
class bit_writer {
public:
  /// Ghi `count` bit thấp của `value`.
  /// @param value Giá trị.
  /// @param count Số bit, 1..32 (ngoài khoảng bị kẹp).
  void write_bits(u32 value, u32 count);
  /// Ghi một `bool`: 1 bit. @param value Giá trị.
  void write_bool(bool value);
  /// Ghi số nguyên trong khoảng `min..max` bằng đúng số bit khoảng đó cần.
  /// Giá trị ngoài khoảng bị kẹp.
  /// @param value Giá trị.
  /// @param min Nhỏ nhất.
  /// @param max Lớn nhất.
  void write_int(i32 value, i32 min, i32 max);
  /// Ghi số không dấu 8 bit. @param value Giá trị.
  void write_u8(u8 value);
  /// Ghi số không dấu 16 bit. @param value Giá trị.
  void write_u16(u16 value);
  /// Ghi số không dấu 32 bit. @param value Giá trị.
  void write_u32(u32 value);
  /// Ghi số không dấu 64 bit. @param value Giá trị.
  void write_u64(u64 value);
  /// Ghi số không dấu với độ dài thay đổi: số nhỏ ít byte (0..127 là 1 byte). Hợp
  /// với id, số đếm thường nhỏ nhưng không có giới hạn trên.
  /// @param value Giá trị.
  void write_varint(u64 value);
  /// Ghi số thực đủ 32 bit, không mất gì. @param value Giá trị.
  void write_f32(f32 value);
  /// Ghi số thực trong khoảng `min..max` với độ chính xác `precision` (lượng
  /// tử hóa): chỉ tốn số bit cần cho số bước đó. Giá trị ngoài khoảng bị kẹp.
  /// @param value Giá trị.
  /// @param min Nhỏ nhất.
  /// @param max Lớn nhất.
  /// @param precision Sai số chấp nhận được, ví dụ `0.01f` (1 cm).
  void write_float(f32 value, f32 min, f32 max, f32 precision);
  /// Ghi vec2, mỗi thành phần như write_float().
  /// @param value Giá trị.
  /// @param min Nhỏ nhất của mỗi thành phần.
  /// @param max Lớn nhất của mỗi thành phần.
  /// @param precision Sai số chấp nhận được.
  void write_vec2(vec2 value, f32 min, f32 max, f32 precision);
  /// Ghi vec3, mỗi thành phần như write_float().
  /// @param value Giá trị.
  /// @param min Nhỏ nhất của mỗi thành phần.
  /// @param max Lớn nhất của mỗi thành phần.
  /// @param precision Sai số chấp nhận được.
  void write_vec3(vec3 value, f32 min, f32 max, f32 precision);
  /// Ghi một góc (radian, giá trị bất kỳ, được quy về một vòng) bằng `bits` bit:
  /// 8 bit là sai số khoảng 1.4°, 12 bit là khoảng 0.09°.
  /// @param radians Góc.
  /// @param bits Số bit, 1..32.
  void write_angle(f32 radians, u32 bits);
  /// Ghi chuỗi dài tối đa `max_length` byte (dài hơn bị cắt).
  /// @param text Chuỗi.
  /// @param max_length Độ dài tối đa; bên đọc phải dùng cùng số này.
  void write_string(std::string_view text, u32 max_length);
  /// Ghi các byte thô (độ dài không được ghi kèm: bên đọc phải biết trước).
  /// @param bytes Các byte.
  void write_bytes(std::string_view bytes);

  /// Các byte đã ghi; bit lẻ cuối cùng được đệm 0 cho đủ byte.
  /// @return Dữ liệu để gửi hay lưu.
  const std::string &data() const { return buffer_; }
  /// Số bit đã ghi. @return Số bit.
  usize bit_count() const { return bits_; }
  /// Xóa hết, ghi lại từ đầu (giữ bộ nhớ đã cấp).
  void clear() {
    buffer_.clear();
    bits_ = 0;
  }

private:
  std::string buffer_;
  usize bits_ = 0;
};

/// Đọc dữ liệu do bit_writer ghi.
///
/// Dữ liệu từ mạng có thể sai hay cố ý độc hại, nên bit_reader **không bao giờ**
/// đọc ra ngoài dữ liệu: đọc quá cuối, số ngoài khoảng hay chuỗi quá dài làm
/// ok() thành `false`, và từ đó mọi lần đọc trả về 0. Đọc hết rồi mới kiểm tra
/// ok() một lần là đủ.
/// @code
/// njin::bit_reader r(packet.data);
/// const njin::i32 id = r.read_int(0, 63);
/// const njin::vec3 pos = r.read_vec3(-500.0f, 500.0f, 0.01f);
/// const bool jumping = r.read_bool();
/// if (!r.ok()) return; // gói hỏng: bỏ
/// @endcode
class bit_reader {
public:
  /// Đọc từ `data`. Dữ liệu phải còn sống khi đang đọc.
  /// @param data Dữ liệu.
  explicit bit_reader(std::string_view data) : data_(data) {}

  /// Đọc `count` bit. @param count Số bit, 1..32. @return Giá trị.
  u32 read_bits(u32 count);
  /// Đọc một `bool`. @return Giá trị.
  bool read_bool();
  /// Đọc số nguyên ghi bởi write_int() với cùng khoảng. Giá trị ngoài khoảng
  /// (dữ liệu hỏng) làm ok() thành `false`.
  /// @param min Nhỏ nhất.
  /// @param max Lớn nhất.
  /// @return Giá trị, trong `min..max`.
  i32 read_int(i32 min, i32 max);
  /// Đọc số không dấu 8 bit. @return Giá trị.
  u8 read_u8();
  /// Đọc số không dấu 16 bit. @return Giá trị.
  u16 read_u16();
  /// Đọc số không dấu 32 bit. @return Giá trị.
  u32 read_u32();
  /// Đọc số không dấu 64 bit. @return Giá trị.
  u64 read_u64();
  /// Đọc số ghi bởi write_varint(). @return Giá trị.
  u64 read_varint();
  /// Đọc số thực 32 bit. NaN và vô cực (dữ liệu hỏng) làm ok() thành `false`.
  /// @return Giá trị.
  f32 read_f32();
  /// Đọc số ghi bởi write_float() với cùng tham số.
  /// @param min Nhỏ nhất.
  /// @param max Lớn nhất.
  /// @param precision Độ chính xác.
  /// @return Giá trị, trong `min..max`.
  f32 read_float(f32 min, f32 max, f32 precision);
  /// Đọc vec2 ghi bởi write_vec2() với cùng tham số.
  /// @param min Nhỏ nhất.
  /// @param max Lớn nhất.
  /// @param precision Độ chính xác.
  /// @return Giá trị.
  vec2 read_vec2(f32 min, f32 max, f32 precision);
  /// Đọc vec3 ghi bởi write_vec3() với cùng tham số.
  /// @param min Nhỏ nhất.
  /// @param max Lớn nhất.
  /// @param precision Độ chính xác.
  /// @return Giá trị.
  vec3 read_vec3(f32 min, f32 max, f32 precision);
  /// Đọc góc ghi bởi write_angle() với cùng số bit.
  /// @param bits Số bit.
  /// @return Góc, radian, trong `0..2π`.
  f32 read_angle(u32 bits);
  /// Đọc chuỗi ghi bởi write_string() với cùng `max_length`.
  /// @param max_length Độ dài tối đa.
  /// @return Chuỗi (rỗng nếu dữ liệu hỏng).
  std::string read_string(u32 max_length);
  /// Đọc `count` byte thô.
  /// @param count Số byte.
  /// @return Các byte (rỗng nếu không đủ dữ liệu).
  std::string read_bytes(usize count);

  /// Chưa có lần đọc nào hỏng. @return `true` nếu mọi giá trị đã đọc đều đúng.
  bool ok() const { return ok_; }
  /// Số bit chưa đọc (gồm cả bit đệm cuối byte). @return Số bit.
  usize bits_left() const { return data_.size() * 8 - pos_; }

private:
  std::string_view data_;
  usize pos_ = 0;
  bool ok_ = true;
};

/// @}
} // namespace njin
