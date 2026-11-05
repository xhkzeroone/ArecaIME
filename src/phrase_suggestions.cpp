#include "phrase_suggestions.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#define ARECA_PHRASE_HAS_STANDARD_PATHS 1
#else
#include <fcitx-utils/standardpath.h>
#endif
#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

uint32_t lowerVietnamese(uint32_t c) {
  if ((c >= 'A' && c <= 'Z') || (c >= 0x00C0 && c <= 0x00D6) ||
      (c >= 0x00D8 && c <= 0x00DE)) {
    return c + 32;
  }
  if (c == 0x0102 || c == 0x0110 || c == 0x0128 || c == 0x0168 ||
      c == 0x01A0 || c == 0x01AF || (c >= 0x1EA0 && c <= 0x1EF8 && c % 2 == 0)) {
    return c + 1;
  }
  if ((c >= 0x0100 && c <= 0x012F && c % 2 == 0) ||
      (c >= 0x0132 && c <= 0x0137 && c % 2 == 0) ||
      (c >= 0x014A && c <= 0x0177 && c % 2 == 0) ||
      (c >= 0x1E00 && c <= 0x1E95 && c % 2 == 0)) {
    return c + 1;
  }
  return c;
}

bool isWordCharacter(uint32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || (c >= 0x00C0 && c <= 0x02AF) ||
         (c >= 0x1E00 && c <= 0x1EFF);
}

std::string normalize(const std::string &text) {
  std::string result;
  if (!fcitx::utf8::validate(text)) {
    return result;
  }
  for (auto it = text.begin(); it != text.end();) {
    uint32_t codepoint = 0;
    auto next = fcitx::utf8::getNextChar(it, text.end(), &codepoint);
    if (isWordCharacter(codepoint)) {
      result += fcitx::utf8::UCS4ToUTF8(lowerVietnamese(codepoint));
    } else if (!result.empty() && result.back() != ' ') {
      result.push_back(' ');
    }
    it = next;
  }
  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }
  return result;
}

std::vector<std::string> words(const std::string &text) {
  std::vector<std::string> result;
  std::istringstream stream(normalize(text));
  for (std::string word; stream >> word;) {
    result.push_back(std::move(word));
  }
  return result;
}

std::vector<std::string> displayWords(const std::string &text) {
  std::vector<std::string> result;
  std::string word;
  if (!fcitx::utf8::validate(text)) {
    return result;
  }
  for (auto it = text.begin(); it != text.end();) {
    uint32_t codepoint = 0;
    auto next = fcitx::utf8::getNextChar(it, text.end(), &codepoint);
    if (isWordCharacter(codepoint)) {
      word.append(it, next);
    } else if (!word.empty()) {
      result.push_back(std::move(word));
      word.clear();
    }
    it = next;
  }
  if (!word.empty()) {
    result.push_back(std::move(word));
  }
  return result;
}

std::string join(const std::vector<std::string> &tokens, size_t begin,
                 size_t end) {
  std::string result;
  for (size_t i = begin; i < end; ++i) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += tokens[i];
  }
  return result;
}

bool hasTrailingSpace(const std::string &text) {
  return !text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                           text.back() == '\n');
}

template <typename Entries>
void addEntry(Entries &entries, std::unordered_set<std::string> &seen,
              const std::string &phrase, int score) {
  auto tokenized = words(phrase);
  if (tokenized.size() < 3) {
    return;
  }
  const auto key = join(tokenized, 0, tokenized.size());
  if (!seen.insert(key).second) {
    return;
  }
  entries.push_back({phrase, std::move(tokenized), displayWords(phrase), score});
}

std::filesystem::path userPhraseDataPath() {
#if defined(ARECA_PHRASE_HAS_STANDARD_PATHS)
  return fcitx::StandardPaths::global().userDirectory(
             fcitx::StandardPathsType::PkgConfig) /
         "areca/phrase-suggestions.tsv";
#else
  return std::filesystem::path(fcitx::StandardPath::global().userDirectory(
             fcitx::StandardPath::Type::PkgConfig)) /
         "areca/phrase-suggestions.tsv";
#endif
}

} // namespace

PhraseSuggestions::PhraseSuggestions() { reload(); }

void PhraseSuggestions::reload() {
  entries_.clear();
  learned_.clear();
  std::unordered_set<std::string> seen;
  const auto load = [this, &seen](std::istream &data) {
    for (std::string line; std::getline(data, line);) {
      if (line.empty() || line.front() == '#') {
        continue;
      }
      const auto tab = line.find('\t');
      int score = 0;
      if (tab != std::string::npos) {
        try {
          score = std::stoi(line.substr(tab + 1));
        } catch (...) {
          score = 0;
        }
        line.resize(tab);
      }
      addEntry(entries_, seen, line, score);
    }
  };

  std::ifstream data(ARECA_PHRASE_DATA_PATH);
  if (!data) {
    data.clear();
    data.open(ARECA_SOURCE_PHRASE_DATA_PATH);
  }
  if (data) {
    load(data);
  }

  std::ifstream userData(userPhraseDataPath());
  if (userData) {
    load(userData);
  }
}

void PhraseSuggestions::observe(const std::string &session,
                                const std::string &observedText) {
  const auto boundary = observedText.find_last_of(".!?;:\n\r");
  const auto sentence = observedText.substr(
      boundary == std::string::npos ? 0 : boundary + 1);
  const auto tokens = words(sentence);
  const auto shownTokens = displayWords(sentence);
  if (tokens.size() < 2) {
    return;
  }
  auto &history = learned_[session];
  const size_t start = tokens.size() > 12 ? tokens.size() - 12 : 0;
  for (size_t i = start; i + 1 < tokens.size(); ++i) {
    for (size_t end = i + 2; end <= tokens.size() && end - i <= 8; ++end) {
      const auto phrase = join(tokens, i, end);
      if (history.size() < 512 || history.contains(phrase)) {
        auto &learned = history[phrase];
        learned.phrase = join(shownTokens, i, end);
        learned.frequency = std::max(learned.frequency, 1U);
      }
    }
  }
}

void PhraseSuggestions::forgetSession(const std::string &session) {
  forgetObservedSession(session);
  selections_.erase(session);
  rotation_.erase(session);
}

void PhraseSuggestions::forgetObservedSession(const std::string &session) {
  learned_.erase(session);
}

void PhraseSuggestions::recordSelection(const std::string &session,
                                       const std::string &phrase) {
  auto &history = selections_[session];
  const auto key = normalize(phrase);
  if (key.empty() || (history.size() >= 256 && !history.contains(key))) {
    return;
  }
  auto &count = history[key];
  count = std::min(count + 1, 50U);
}

std::vector<PhraseSuggestion> PhraseSuggestions::suggest(
    const std::string &session, const std::string &observedText,
    size_t limit) const {
  std::string context = observedText;
  while (!context.empty() && (context.back() == ' ' || context.back() == '\t' ||
                              context.back() == '\n')) {
    context.pop_back();
  }
  const auto contextWords = words(context);
  if (contextWords.empty()) {
    return {};
  }

  struct Ranked {
    PhraseSuggestion suggestion;
    size_t matchedWords;
    bool learned;
    unsigned int selectionCount;
  };
  std::vector<Ranked> ranked;
  std::unordered_set<std::string> seen;
  const auto learnedIt = learned_.find(session);
  const auto selectionIt = selections_.find(session);

  auto consider = [&](const std::vector<std::string> &phraseWords,
                      const std::vector<std::string> &shownWords, int score,
                      bool learned) {
    const size_t maxPrefix = std::min<size_t>(8, phraseWords.size() - 1);
    for (size_t count = 1; count <= maxPrefix && count <= contextWords.size();
         ++count) {
      const size_t contextStart = contextWords.size() - count;
      bool matches = true;
      for (size_t i = 0; i < count; ++i) {
        if (contextWords[contextStart + i] != phraseWords[i]) {
          matches = false;
          break;
        }
      }
      if (!matches) {
        continue;
      }

      const auto completion = join(shownWords, count, shownWords.size());
      const auto phraseKey = join(phraseWords, 0, phraseWords.size());
      if (completion.empty() || !seen.insert(phraseKey).second) {
        continue;
      }
      std::string label = join(shownWords, 0, shownWords.size());
      int finalScore = score + static_cast<int>(count * 100);
      const auto selected = selectionIt == selections_.end()
                                ? 0U
                                : (selectionIt->second.contains(phraseKey)
                                       ? selectionIt->second.at(phraseKey)
                                       : 0U);
      ranked.push_back({{std::move(label),
                         (hasTrailingSpace(observedText) ? "" : " ") +
                             completion,
                         finalScore},
                        count, learned, selected});
      break;
    }
  };

  for (const auto &entry : entries_) {
    consider(entry.words, entry.displayWords, entry.score, false);
  }
  if (learnedIt != learned_.end()) {
    for (const auto &[key, learned] : learnedIt->second) {
      auto phraseWords = words(key);
      auto shownWords = displayWords(learned.phrase);
      consider(phraseWords, shownWords,
               500 + static_cast<int>(
                         std::min<unsigned int>(learned.frequency, 100)),
               true);
    }
  }

  std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
    if (a.matchedWords != b.matchedWords) {
      return a.matchedWords > b.matchedWords;
    }
    if (a.selectionCount != b.selectionCount) {
      return a.selectionCount > b.selectionCount;
    }
    if (a.learned != b.learned) {
      return !a.learned;
    }
    return a.suggestion.score > b.suggestion.score;
  });
  const auto rotation = rotation_[session]++;
  for (size_t begin = 0; begin < ranked.size();) {
    size_t end = begin + 1;
    while (end < ranked.size() &&
           ranked[end].matchedWords == ranked[begin].matchedWords &&
           ranked[end].selectionCount == ranked[begin].selectionCount) {
      ++end;
    }
    const auto groupSize = end - begin;
    if (groupSize > 1) {
      std::rotate(ranked.begin() + begin,
                  ranked.begin() + begin + (rotation % groupSize),
                  ranked.begin() + end);
    }
    begin = end;
  }
  if (ranked.size() > limit) {
    ranked.resize(limit);
  }
  std::vector<PhraseSuggestion> result;
  result.reserve(ranked.size());
  for (auto &item : ranked) {
    result.push_back(std::move(item.suggestion));
  }
  return result;
}

} // namespace areca
