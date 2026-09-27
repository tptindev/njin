#pragma once
#include "_types.h"
#include <cstdio>
#include <string>
#include <vector>

namespace njin {
// Writes an animated GIF one frame at a time, so a long recording never holds
// more than two frames in memory. Each frame gets its own 256-colour palette
// (median cut over a 5-5-5 bit histogram: exact for flat pixel art, close for
// anything else) and is LZW-compressed as it arrives. A frame equal to the
// previous one is not written again: the previous frame is shown longer.
class gif_writer {
public:
  gif_writer() = default;
  ~gif_writer() { close(); }
  gif_writer(const gif_writer &) = delete;
  gif_writer &operator=(const gif_writer &) = delete;

  // Creates the file (its folder must exist). `loop` repeats the animation forever.
  bool open(const std::string &path, i32 width, i32 height, bool loop = true);
  // Adds a frame of `width * height` RGBA pixels shown for `delay_cs` hundredths of a second (at least 1).
  bool add_frame(const u8 *rgba, i32 delay_cs);
  // Writes the last frame and the end marker. Safe to call twice.
  bool close();

  bool is_open() const { return file_ != nullptr; }
  bool failed() const { return failed_; }
  usize bytes() const { return bytes_; }   // written so far
  i32 frames() const { return frames_; }   // frames given, counting repeats

private:
  bool flush_pending();
  void write(const void *data, usize size);

  std::FILE *file_ = nullptr;
  i32 width_ = 0, height_ = 0;
  bool failed_ = false;
  usize bytes_ = 0;
  i32 frames_ = 0;
  std::vector<u8> pending_;  // RGBA of the frame waiting for its delay to be final
  i32 pending_delay_ = 0;
  std::vector<u8> indexed_;  // scratch: palette indexes of one frame
};
} // namespace njin
