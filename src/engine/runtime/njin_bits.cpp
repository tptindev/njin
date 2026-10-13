#include "njin_bits.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace njin {
namespace {
constexpr f64 tau = 6.283185307179586;

// Bits needed to hold 0..range.
u32 bits_for(u64 range) {
  u32 bits = 0;
  while (range > 0) {
    ++bits;
    range >>= 1;
  }
  return bits;
}

// Steps of a quantized float, 0 when the range or precision make no sense.
u64 float_steps(f32 min, f32 max, f32 precision) {
  if (!(precision > 0.0f) || !(max > min))
    return 0;
  const f64 steps = std::ceil(((f64)max - (f64)min) / (f64)precision);
  return steps >= 4294967295.0 ? 4294967295ull : (u64)steps;
}
} // namespace

void bit_writer::write_bits(u32 value, u32 count) {
  count = std::min(count, 32u);
  if (count < 32)
    value &= (1u << count) - 1u;
  u32 done = 0;
  while (done < count) {
    const u32 offset = (u32)(bits_ % 8);
    if (offset == 0)
      buffer_.push_back('\0');
    const u32 n = std::min(8 - offset, count - done);
    const u32 part = (value >> done) & ((1u << n) - 1u);
    buffer_.back() = (char)((unsigned char)buffer_.back() | (part << offset));
    done += n;
    bits_ += n;
  }
}

void bit_writer::write_bool(bool value) { write_bits(value ? 1u : 0u, 1); }

void bit_writer::write_int(i32 value, i32 min, i32 max) {
  if (max < min)
    std::swap(min, max);
  value = std::clamp(value, min, max);
  write_bits((u32)((i64)value - min), bits_for((u64)((i64)max - min)));
}

void bit_writer::write_u8(u8 value) { write_bits(value, 8); }
void bit_writer::write_u16(u16 value) { write_bits(value, 16); }
void bit_writer::write_u32(u32 value) { write_bits(value, 32); }

void bit_writer::write_u64(u64 value) {
  write_bits((u32)value, 32);
  write_bits((u32)(value >> 32), 32);
}

void bit_writer::write_varint(u64 value) {
  do {
    const u32 low = (u32)(value & 0x7fu);
    value >>= 7;
    write_bits(low | (value != 0 ? 0x80u : 0u), 8);
  } while (value != 0);
}

void bit_writer::write_f32(f32 value) {
  u32 raw = 0;
  std::memcpy(&raw, &value, sizeof raw);
  write_bits(raw, 32);
}

void bit_writer::write_float(f32 value, f32 min, f32 max, f32 precision) {
  const u64 steps = float_steps(min, max, precision);
  if (steps == 0)
    return;
  const f32 v = std::isnan(value) ? min : std::clamp(value, min, max);
  const u64 q = std::min((u64)std::llround(((f64)v - (f64)min) / (f64)precision), steps);
  write_bits((u32)q, bits_for(steps));
}

void bit_writer::write_vec2(vec2 value, f32 min, f32 max, f32 precision) {
  write_float(value.x, min, max, precision);
  write_float(value.y, min, max, precision);
}

void bit_writer::write_vec3(vec3 value, f32 min, f32 max, f32 precision) {
  write_float(value.x, min, max, precision);
  write_float(value.y, min, max, precision);
  write_float(value.z, min, max, precision);
}

void bit_writer::write_angle(f32 radians, u32 bits) {
  bits = std::clamp(bits, 1u, 32u);
  f64 turn = std::isfinite(radians) ? (f64)radians / tau : 0.0;
  turn -= std::floor(turn);
  const u64 steps = 1ull << bits;
  write_bits((u32)((u64)std::llround(turn * (f64)steps) % steps), bits);
}

void bit_writer::write_string(std::string_view text, u32 max_length) {
  text = text.substr(0, max_length);
  write_bits((u32)text.size(), bits_for(max_length));
  write_bytes(text);
}

void bit_writer::write_bytes(std::string_view bytes) {
  bits_ = buffer_.size() * 8; // start on a whole byte
  buffer_.append(bytes);
  bits_ += bytes.size() * 8;
}

u32 bit_reader::read_bits(u32 count) {
  count = std::min(count, 32u);
  if (!ok_ || count > bits_left()) {
    ok_ = false;
    return 0;
  }
  u32 value = 0, done = 0;
  while (done < count) {
    const u32 offset = (u32)(pos_ % 8);
    const u32 n = std::min(8 - offset, count - done);
    const u32 byte = (unsigned char)data_[pos_ / 8];
    value |= ((byte >> offset) & ((1u << n) - 1u)) << done;
    done += n;
    pos_ += n;
  }
  return value;
}

bool bit_reader::read_bool() { return read_bits(1) != 0; }

i32 bit_reader::read_int(i32 min, i32 max) {
  if (max < min)
    std::swap(min, max);
  const u64 range = (u64)((i64)max - min);
  const u32 raw = read_bits(bits_for(range));
  if (raw > range) {
    ok_ = false;
    return min;
  }
  return ok_ ? (i32)((i64)min + raw) : min;
}

u8 bit_reader::read_u8() { return (u8)read_bits(8); }
u16 bit_reader::read_u16() { return (u16)read_bits(16); }
u32 bit_reader::read_u32() { return read_bits(32); }

u64 bit_reader::read_u64() {
  const u64 low = read_bits(32);
  return low | (u64)read_bits(32) << 32;
}

u64 bit_reader::read_varint() {
  u64 value = 0;
  for (u32 shift = 0; shift < 64; shift += 7) {
    const u32 byte = read_bits(8);
    if (!ok_)
      return 0;
    value |= (u64)(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0)
      return value;
  }
  ok_ = false; // more than ten groups: not something write_varint made
  return 0;
}

f32 bit_reader::read_f32() {
  const u32 raw = read_bits(32);
  f32 value = 0.0f;
  std::memcpy(&value, &raw, sizeof value);
  if (!std::isfinite(value)) {
    ok_ = false;
    return 0.0f;
  }
  return value;
}

f32 bit_reader::read_float(f32 min, f32 max, f32 precision) {
  const u64 steps = float_steps(min, max, precision);
  if (steps == 0)
    return min;
  const u32 raw = read_bits(bits_for(steps));
  if (raw > steps) {
    ok_ = false;
    return min;
  }
  return std::min((f32)((f64)min + (f64)raw * (f64)precision), max);
}

vec2 bit_reader::read_vec2(f32 min, f32 max, f32 precision) {
  const f32 x = read_float(min, max, precision);
  const f32 y = read_float(min, max, precision);
  return {x, y};
}

vec3 bit_reader::read_vec3(f32 min, f32 max, f32 precision) {
  const f32 x = read_float(min, max, precision);
  const f32 y = read_float(min, max, precision);
  const f32 z = read_float(min, max, precision);
  return {x, y, z};
}

f32 bit_reader::read_angle(u32 bits) {
  bits = std::clamp(bits, 1u, 32u);
  return (f32)((f64)read_bits(bits) / (f64)(1ull << bits) * tau);
}

std::string bit_reader::read_string(u32 max_length) {
  const u32 length = read_bits(bits_for(max_length));
  if (length > max_length) {
    ok_ = false;
    return {};
  }
  return read_bytes(length);
}

std::string bit_reader::read_bytes(usize count) {
  pos_ = (pos_ + 7) / 8 * 8; // whole bytes, as write_bytes left them
  if (!ok_ || pos_ > data_.size() * 8 || count > data_.size() - pos_ / 8) {
    ok_ = false;
    pos_ = std::min(pos_, data_.size() * 8);
    return {};
  }
  std::string out(data_.substr(pos_ / 8, count));
  pos_ += count * 8;
  return out;
}
} // namespace njin
