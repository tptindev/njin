#pragma once
#include <njin.h>
#include <string>

namespace fps {
// Aim trainer in first person: walk on a plane, shoot the red targets of a
// 5x5 grid; each hit moves that target to a free cell.
njin::mod_desc fps_module();

// Playing together (njin_netcode.h): one player hosts and plays, the others
// join; all shoot at the same targets and the host keeps the score.
struct net_mode {
  enum kind_t { solo, host, join } kind = solo;
  std::string address = "127.0.0.1"; // join: the host
  njin::u16 port = 7777;
  njin::net_sim sim; // a bad network, simulated on this machine
  bool bot = false;   // the player walks and shoots by itself
};
njin::mod_desc fps_module(const net_mode &mode);
} // namespace fps
