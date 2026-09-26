#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace njin {
// One language's strings, keys flattened with dots.
struct i18n_table {
  std::string lang;
  std::unordered_map<std::string, std::string> strings;
};

struct i18n_store {
  std::vector<i18n_table> tables; // in load order
  std::string current;
  std::string fallback;
  // Keys already reported missing, so the log names each once. tr() is const.
  mutable std::unordered_set<std::string> missing;
};
} // namespace njin
