#pragma once
#include "_math.h"
#include "_types.h"
#include <string>
#include <string_view>

namespace njin {

/// @addtogroup grp_bits
/// @{

/// Writes binary data bit by bit: network packets, replays, compact saves.
///
/// Each value takes only the bits it needs: a `bool` is 1 bit, a number in
/// 0..100 is 7 bits, a coordinate in -500..500 m accurate to 1 cm is 17 bits
/// instead of 32. Read it back with bit_reader, **in the same order and with
/// the same parameters** it was written with.
/// @code
/// njin::bit_writer w;
/// w.write_int(player_id, 0, 63);
/// w.write_vec3(position, -500.0f, 500.0f, 0.01f);
/// w.write_bool(jumping);
/// njin::net_peer_send(ctx, server, w.data(), njin::net_delivery::unreliable);
/// @endcode
class bit_writer {
public:
  /// Writes the `count` low bits of `value`.
  /// @param value Value.
  /// @param count Bit count, 1..32 (clamped outside that range).
  void write_bits(u32 value, u32 count);
  /// Writes a `bool`: 1 bit. @param value Value.
  void write_bool(bool value);
  /// Writes an integer in the `min..max` range using exactly the number of
  /// bits that range needs. A value outside the range is clamped.
  /// @param value Value.
  /// @param min Lowest.
  /// @param max Highest.
  void write_int(i32 value, i32 min, i32 max);
  /// Writes an 8-bit unsigned number. @param value Value.
  void write_u8(u8 value);
  /// Writes a 16-bit unsigned number. @param value Value.
  void write_u16(u16 value);
  /// Writes a 32-bit unsigned number. @param value Value.
  void write_u32(u32 value);
  /// Writes a 64-bit unsigned number. @param value Value.
  void write_u64(u64 value);
  /// Writes an unsigned number with a variable length: small numbers take
  /// fewer bytes (0..127 is 1 byte). Good for ids and counts that are
  /// usually small but have no upper bound.
  /// @param value Value.
  void write_varint(u64 value);
  /// Writes a full 32-bit float, losing nothing. @param value Value.
  void write_f32(f32 value);
  /// Writes a float in the `min..max` range at `precision` (quantization):
  /// only costs the bits that step count needs. A value outside the range
  /// is clamped.
  /// @param value Value.
  /// @param min Lowest.
  /// @param max Highest.
  /// @param precision Acceptable error, e.g. `0.01f` (1 cm).
  void write_float(f32 value, f32 min, f32 max, f32 precision);
  /// Writes a vec2, each component as in write_float().
  /// @param value Value.
  /// @param min Lowest of each component.
  /// @param max Highest of each component.
  /// @param precision Acceptable error.
  void write_vec2(vec2 value, f32 min, f32 max, f32 precision);
  /// Writes a vec3, each component as in write_float().
  /// @param value Value.
  /// @param min Lowest of each component.
  /// @param max Highest of each component.
  /// @param precision Acceptable error.
  void write_vec3(vec3 value, f32 min, f32 max, f32 precision);
  /// Writes an angle (radians, any value, wrapped to one turn) using `bits`
  /// bits: 8 bits is about 1.4° of error, 12 bits is about 0.09°.
  /// @param radians Angle.
  /// @param bits Bit count, 1..32.
  void write_angle(f32 radians, u32 bits);
  /// Writes a string up to `max_length` bytes long (longer is truncated).
  /// @param text String.
  /// @param max_length Maximum length; the reader must use the same number.
  void write_string(std::string_view text, u32 max_length);
  /// Writes raw bytes (the length is not written along with them: the
  /// reader must already know it).
  /// @param bytes The bytes.
  void write_bytes(std::string_view bytes);

  /// The bytes written so far; a trailing partial bit is padded with 0 to
  /// fill the byte.
  /// @return Data to send or save.
  const std::string &data() const { return buffer_; }
  /// Number of bits written. @return Bit count.
  usize bit_count() const { return bits_; }
  /// Clears everything, writing again from the start (keeps the allocated
  /// memory).
  void clear() {
    buffer_.clear();
    bits_ = 0;
  }

private:
  std::string buffer_;
  usize bits_ = 0;
};

/// Reads data written by bit_writer.
///
/// Data from the network can be wrong or deliberately malicious, so
/// bit_reader **never** reads past its data: reading past the end, a value
/// outside its range or a string that is too long turns ok() to `false`,
/// and from then on every read returns 0. Checking ok() once after reading
/// everything is enough.
/// @code
/// njin::bit_reader r(packet.data);
/// const njin::i32 id = r.read_int(0, 63);
/// const njin::vec3 pos = r.read_vec3(-500.0f, 500.0f, 0.01f);
/// const bool jumping = r.read_bool();
/// if (!r.ok()) return; // corrupt packet: drop it
/// @endcode
class bit_reader {
public:
  /// Reads from `data`. The data must stay alive while reading.
  /// @param data Data.
  explicit bit_reader(std::string_view data) : data_(data) {}

  /// Reads `count` bits. @param count Bit count, 1..32. @return Value.
  u32 read_bits(u32 count);
  /// Reads a `bool`. @return Value.
  bool read_bool();
  /// Reads an integer written by write_int() with the same range. A value
  /// outside the range (corrupt data) turns ok() to `false`.
  /// @param min Lowest.
  /// @param max Highest.
  /// @return Value, within `min..max`.
  i32 read_int(i32 min, i32 max);
  /// Reads an 8-bit unsigned number. @return Value.
  u8 read_u8();
  /// Reads a 16-bit unsigned number. @return Value.
  u16 read_u16();
  /// Reads a 32-bit unsigned number. @return Value.
  u32 read_u32();
  /// Reads a 64-bit unsigned number. @return Value.
  u64 read_u64();
  /// Reads a number written by write_varint(). @return Value.
  u64 read_varint();
  /// Reads a 32-bit float. NaN and infinity (corrupt data) turn ok() to
  /// `false`.
  /// @return Value.
  f32 read_f32();
  /// Reads a value written by write_float() with the same parameters.
  /// @param min Lowest.
  /// @param max Highest.
  /// @param precision Precision.
  /// @return Value, within `min..max`.
  f32 read_float(f32 min, f32 max, f32 precision);
  /// Reads a vec2 written by write_vec2() with the same parameters.
  /// @param min Lowest.
  /// @param max Highest.
  /// @param precision Precision.
  /// @return Value.
  vec2 read_vec2(f32 min, f32 max, f32 precision);
  /// Reads a vec3 written by write_vec3() with the same parameters.
  /// @param min Lowest.
  /// @param max Highest.
  /// @param precision Precision.
  /// @return Value.
  vec3 read_vec3(f32 min, f32 max, f32 precision);
  /// Reads an angle written by write_angle() with the same bit count.
  /// @param bits Bit count.
  /// @return Angle, radians, within `0..2π`.
  f32 read_angle(u32 bits);
  /// Reads a string written by write_string() with the same `max_length`.
  /// @param max_length Maximum length.
  /// @return String (empty if the data is corrupt).
  std::string read_string(u32 max_length);
  /// Reads `count` raw bytes.
  /// @param count Byte count.
  /// @return The bytes (empty if there is not enough data).
  std::string read_bytes(usize count);

  /// No read has failed so far. @return `true` if every value read so far
  /// is valid.
  bool ok() const { return ok_; }
  /// Number of bits not yet read (including the trailing padding bits).
  /// @return Bit count.
  usize bits_left() const { return data_.size() * 8 - pos_; }

private:
  std::string_view data_;
  usize pos_ = 0;
  bool ok_ = true;
};

/// @}
} // namespace njin
