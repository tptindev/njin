#include "njin_gif.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace njin {
namespace {
constexpr i32 bins = 32768; // 5 bits per channel

i32 bin_of(const u8 *p) { return ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3); }
i32 channel(i32 bin, i32 axis) { return axis == 0 ? (bin >> 10) & 31 : (axis == 1 ? (bin >> 5) & 31 : bin & 31); }
u8 expand(i32 v) { return (u8)((v << 3) | (v >> 2)); }

struct box {
  usize begin = 0, end = 0; // range in the sorted list of used bins
  i32 axis = 0;             // its longest channel
  i32 range = 0;            // extent along that channel
  u64 pop = 0;
};

void measure(box &b, const std::vector<u16> &used, const std::vector<u32> &hist) {
  i32 lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
  b.pop = 0;
  for (usize i = b.begin; i < b.end; i++) {
    const i32 bin = used[i];
    b.pop += hist[(usize)bin];
    for (i32 c = 0; c < 3; c++) {
      lo[c] = std::min(lo[c], channel(bin, c));
      hi[c] = std::max(hi[c], channel(bin, c));
    }
  }
  b.axis = 0;
  b.range = hi[0] - lo[0];
  for (i32 c = 1; c < 3; c++)
    if (hi[c] - lo[c] > b.range) {
      b.axis = c;
      b.range = hi[c] - lo[c];
    }
}

// Reduces a frame to at most 256 colours. Fills `palette` (768 bytes) and `out` with one index per pixel.
void quantize(const u8 *rgba, usize pixels, u8 *palette, std::vector<u8> &out) {
  std::vector<u32> hist(bins, 0);
  for (usize i = 0; i < pixels; i++)
    hist[(usize)bin_of(rgba + i * 4)]++;
  std::vector<u16> used;
  for (i32 b = 0; b < bins; b++)
    if (hist[(usize)b] > 0)
      used.push_back((u16)b);

  std::vector<box> boxes;
  boxes.push_back({0, used.size(), 0, 0, 0});
  measure(boxes[0], used, hist);
  while (boxes.size() < 256) {
    // The box with the longest side that still holds more than one colour.
    usize pick = boxes.size();
    for (usize i = 0; i < boxes.size(); i++) {
      if (boxes[i].end - boxes[i].begin < 2)
        continue;
      if (pick == boxes.size() || boxes[i].range > boxes[pick].range ||
          (boxes[i].range == boxes[pick].range && boxes[i].pop > boxes[pick].pop))
        pick = i;
    }
    if (pick == boxes.size())
      break;
    box b = boxes[pick];
    const i32 axis = b.axis;
    std::sort(used.begin() + (long)b.begin, used.begin() + (long)b.end,
              [axis](u16 x, u16 y) { return channel(x, axis) < channel(y, axis); });
    // Split where half of the pixels lie on each side (both sides keep at least one colour).
    u64 acc = 0;
    usize cut = b.begin + 1;
    for (usize i = b.begin; i + 1 < b.end; i++) {
      acc += hist[used[i]];
      cut = i + 1;
      if (acc * 2 >= b.pop)
        break;
    }
    box left{b.begin, cut, 0, 0, 0}, right{cut, b.end, 0, 0, 0};
    measure(left, used, hist);
    measure(right, used, hist);
    boxes[pick] = left;
    boxes.push_back(right);
  }

  std::vector<u8> lut(bins, 0);
  std::memset(palette, 0, 768);
  for (usize k = 0; k < boxes.size(); k++) {
    u64 r = 0, g = 0, bl = 0, n = 0;
    for (usize i = boxes[k].begin; i < boxes[k].end; i++) {
      const i32 bin = used[i];
      const u64 w = hist[(usize)bin];
      r += w * (u64)channel(bin, 0);
      g += w * (u64)channel(bin, 1);
      bl += w * (u64)channel(bin, 2);
      n += w;
      lut[(usize)bin] = (u8)k;
    }
    if (n == 0)
      n = 1;
    palette[k * 3 + 0] = expand((i32)((r + n / 2) / n));
    palette[k * 3 + 1] = expand((i32)((g + n / 2) / n));
    palette[k * 3 + 2] = expand((i32)((bl + n / 2) / n));
  }
  out.resize(pixels);
  for (usize i = 0; i < pixels; i++)
    out[i] = lut[(usize)bin_of(rgba + i * 4)];
}

// Packs LZW codes of growing width into GIF sub-blocks.
class bit_sink {
public:
  explicit bit_sink(std::vector<u8> &out) : out_(out) {}
  void put(u32 code, i32 width) {
    acc_ |= (u64)code << bits_;
    bits_ += width;
    while (bits_ >= 8) {
      byte((u8)(acc_ & 0xFF));
      acc_ >>= 8;
      bits_ -= 8;
    }
  }
  void finish() {
    if (bits_ > 0)
      byte((u8)(acc_ & 0xFF));
    acc_ = 0;
    bits_ = 0;
    flush_block();
  }

private:
  void byte(u8 b) {
    block_[count_++] = b;
    if (count_ == 255)
      flush_block();
  }
  void flush_block() {
    if (count_ == 0)
      return;
    out_.push_back((u8)count_);
    out_.insert(out_.end(), block_, block_ + count_);
    count_ = 0;
  }
  std::vector<u8> &out_;
  u64 acc_ = 0;
  i32 bits_ = 0;
  u8 block_[255] = {};
  i32 count_ = 0;
};

// GIF's LZW with 8-bit symbols: codes 258 and up are added as strings are seen, and the table is
// cleared when it is full (4096 codes).
void lzw(const std::vector<u8> &pixels, std::vector<u8> &out) {
  constexpr i32 clear = 256, eoi = 257, table_size = 1 << 13; // hash slots, a power of two
  struct slot {
    i32 key = -1; // (prefix << 8) | byte
    i32 code = 0;
  };
  std::vector<slot> table((usize)table_size);
  bit_sink sink(out);
  i32 width = 9, next = 258;
  sink.put(clear, width);
  if (pixels.empty()) {
    sink.put(eoi, width);
    sink.finish();
    return;
  }
  i32 prefix = pixels[0];
  for (usize i = 1; i < pixels.size(); i++) {
    const i32 c = pixels[i];
    const i32 key = (prefix << 8) | c;
    u32 h = ((u32)key * 2654435761u) >> 19; // 13 bits
    bool found = false;
    while (table[h].key != -1) {
      if (table[h].key == key) {
        found = true;
        break;
      }
      h = (h + 1) & (u32)(table_size - 1);
    }
    if (found) {
      prefix = table[h].code;
      continue;
    }
    sink.put((u32)prefix, width);
    if (next < 4096) {
      table[h] = {key, next};
      if (next == (1 << width) && width < 12)
        width++;
      next++;
    } else {
      sink.put(clear, width);
      std::fill(table.begin(), table.end(), slot{});
      width = 9;
      next = 258;
    }
    prefix = c;
  }
  sink.put((u32)prefix, width);
  // The decoder adds one more entry after this code, so it may already expect a wider code. A clear code
  // (at the width in force) then the end marker at the smallest width sidesteps that.
  sink.put(clear, width);
  sink.put(eoi, 9);
  sink.finish();
}
} // namespace

void gif_writer::write(const void *data, usize size) {
  if (file_ == nullptr || failed_)
    return;
  if (std::fwrite(data, 1, size, file_) != size)
    failed_ = true;
  else
    bytes_ += size;
}

bool gif_writer::open(const std::string &path, i32 width, i32 height, bool loop) {
  close();
  if (width < 1 || height < 1 || width > 65535 || height > 65535)
    return false;
#ifdef _WIN32
  // Paths are UTF-8: fopen on Windows reads the ANSI code page, so go through the wide API.
  std::wstring wide;
  for (usize i = 0; i < path.size();) {
    const u8 c = (u8)path[i];
    u32 cp = c;
    i32 extra = 0;
    if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
    for (i32 k = 1; k <= extra && i + (usize)k < path.size(); k++)
      cp = (cp << 6) | ((u8)path[i + (usize)k] & 0x3F);
    i += (usize)extra + 1;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      wide.push_back((wchar_t)(0xD800 + (cp >> 10)));
      wide.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
    } else {
      wide.push_back((wchar_t)cp);
    }
  }
  file_ = _wfopen(wide.c_str(), L"wb");
#else
  file_ = std::fopen(path.c_str(), "wb");
#endif
  if (file_ == nullptr)
    return false;
  width_ = width;
  height_ = height;
  failed_ = false;
  bytes_ = 0;
  frames_ = 0;
  pending_.clear();
  pending_delay_ = 0;

  u8 head[13] = {'G', 'I', 'F', '8', '9', 'a', (u8)(width & 255), (u8)(width >> 8), (u8)(height & 255), (u8)(height >> 8), 0, 0, 0};
  write(head, sizeof head);
  if (loop) {
    const u8 netscape[19] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0, 0, 0x00};
    write(netscape, sizeof netscape);
  }
  return !failed_;
}

bool gif_writer::flush_pending() {
  if (pending_.empty())
    return true;
  const usize pixels = (usize)width_ * (usize)height_;
  u8 palette[768];
  quantize(pending_.data(), pixels, palette, indexed_);
  const i32 delay = std::clamp(pending_delay_, 1, 65535);
  // Graphic control extension: keep the frame in place, no transparency.
  const u8 gce[8] = {0x21, 0xF9, 0x04, 0x04, (u8)(delay & 255), (u8)(delay >> 8), 0, 0x00};
  write(gce, sizeof gce);
  const u8 desc[10] = {0x2C, 0, 0, 0, 0, (u8)(width_ & 255), (u8)(width_ >> 8), (u8)(height_ & 255), (u8)(height_ >> 8), 0x87};
  write(desc, sizeof desc);
  write(palette, 768);
  const u8 min_code = 8;
  write(&min_code, 1);
  std::vector<u8> data;
  data.reserve(pixels / 2);
  lzw(indexed_, data);
  write(data.data(), data.size());
  const u8 end = 0;
  write(&end, 1);
  pending_.clear();
  pending_delay_ = 0;
  return !failed_;
}

bool gif_writer::add_frame(const u8 *rgba, i32 delay_cs) {
  if (file_ == nullptr || failed_ || rgba == nullptr)
    return false;
  const usize size = (usize)width_ * (usize)height_ * 4;
  delay_cs = std::max(delay_cs, 1);
  frames_++;
  if (!pending_.empty() && std::memcmp(pending_.data(), rgba, size) == 0) {
    pending_delay_ += delay_cs; // nothing moved: show the last frame longer
    return true;
  }
  if (!flush_pending())
    return false;
  pending_.assign(rgba, rgba + size);
  pending_delay_ = delay_cs;
  return true;
}

bool gif_writer::close() {
  if (file_ == nullptr)
    return !failed_;
  flush_pending();
  const u8 trailer = 0x3B;
  write(&trailer, 1);
  const bool ok = std::fclose(file_) == 0 && !failed_;
  file_ = nullptr;
  return ok;
}
} // namespace njin
