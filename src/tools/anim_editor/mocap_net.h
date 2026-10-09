#pragma once
// UDP receiver and the pose_stream.py process. No raylib here: windows.h and
// raylib.h clash, so this pair stays apart from the rest of the editor.
#include <string>
#include <vector>

namespace anim_editor {
class MocapLink {
public:
  ~MocapLink();
  bool listen(int port, std::string &error); // 127.0.0.1 only
  void close();
  bool open() const { return socket_ != ~0ull; }
  // Every packet waiting, oldest first.
  void receive(std::vector<std::vector<unsigned char>> &out);

private:
  unsigned long long socket_ = ~0ull;
};

class Sidecar {
public:
  ~Sidecar();
  // Runs `python script args...`, its output into `log`.
  bool start(const std::string &python, const std::string &script, const std::vector<std::string> &args,
             const std::string &log, std::string &error);
  bool running();
  int exit_code() const { return exit_code_; }
  void stop();

private:
  void *process_ = nullptr;
  int exit_code_ = 0;
};

double clock_seconds(); // Wall clock, as pose_stream.py's time.time() on camera frames.
} // namespace anim_editor
