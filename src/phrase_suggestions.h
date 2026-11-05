#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace areca {

struct PhraseSuggestion {
  std::string phrase;
  std::string completion;
  int score = 0;
};

class PhraseSuggestions {
public:
  PhraseSuggestions();

  void reload();

  void observe(const std::string &session, const std::string &observedText);
  void recordSelection(const std::string &session, const std::string &phrase);
  void forgetObservedSession(const std::string &session);
  void forgetSession(const std::string &session);
  std::vector<PhraseSuggestion>
  suggest(const std::string &session, const std::string &observedText,
          size_t limit = 5) const;

private:
  struct Entry {
    std::string phrase;
    std::vector<std::string> words;
    std::vector<std::string> displayWords;
    int score = 0;
  };
  struct LearnedEntry {
    std::string phrase;
    unsigned int frequency = 0;
  };

  std::vector<Entry> entries_;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, LearnedEntry>> learned_;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, unsigned int>> selections_;
  mutable std::unordered_map<std::string, uint64_t> rotation_;
};

} // namespace areca
