#include "bamboo_engine_adapter.h"

#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fcitx-utils/utf8.h>

extern "C" {
uint64_t ArecaBambooCreate(char *inputMethod, int modernStyle);
void ArecaBambooDestroy(uint64_t id);
int ArecaBambooCanProcess(uint64_t id, uint32_t key);
char *ArecaBambooProcess(uint64_t id, uint32_t key, int spellCheck);
char *ArecaBambooFinalizeWord(uint64_t id, int spellCheck);
char *ArecaBambooBackspace(uint64_t id);
void ArecaBambooReset(uint64_t id);
char *ArecaBambooInputMethodNames();
char *ArecaBambooCharsetNames();
char *ArecaBambooEncode(char *charset, char *input);
void ArecaBambooAddMacro(uint64_t id, char *key, char *value);
char *ArecaBambooExpandMacro(uint64_t id, int capitalize);
}

namespace areca {
namespace {

std::vector<std::pair<uint32_t, size_t>> codepoints(const std::string &text) {
  std::vector<std::pair<uint32_t, size_t>> result;
  auto it = text.begin();
  while (it != text.end()) {
    const auto byteOffset = static_cast<size_t>(it - text.begin());
    uint32_t codepoint = 0;
    auto next = fcitx::utf8::getNextChar(it, text.end(), &codepoint);
    if (!fcitx::utf8::isValidChar(codepoint)) {
      throw std::runtime_error("Bamboo returned invalid UTF-8");
    }
    result.emplace_back(codepoint, byteOffset);
    it = next;
  }
  result.emplace_back(0, text.size());
  return result;
}

std::vector<std::string> splitLines(char *raw) {
  if (!raw) {
    return {};
  }
  std::string joined(raw);
  std::free(raw);
  std::vector<std::string> result;
  size_t begin = 0;
  while (begin <= joined.size()) {
    const auto end = joined.find('\n', begin);
    const auto value = joined.substr(begin, end - begin);
    if (!value.empty()) {
      result.push_back(value);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return result;
}

} // namespace

BambooEngineAdapter::BambooEngineAdapter(std::string inputMethod,
                                         bool spellCheck,
                                         bool realtimeSpellcheck,
                                         bool modernStyle,
                                         std::string outputCharset,
                                         bool macroEnabled,
                                         bool capitalizeMacro,
                                         std::vector<MacroDefinition> macros)
    : spellCheck_(spellCheck), realtimeSpellcheck_(realtimeSpellcheck),
      outputCharset_(std::move(outputCharset)),
      macroEnabled_(macroEnabled), capitalizeMacro_(capitalizeMacro) {
  handle_ = ArecaBambooCreate(inputMethod.data(), modernStyle ? 1 : 0);
  if (!handle_) {
    throw std::runtime_error("unknown Bamboo input method: " + inputMethod);
  }
  for (auto &macro : macros) {
    ArecaBambooAddMacro(handle_, macro.key.data(), macro.value.data());
  }
}

BambooEngineAdapter::~BambooEngineAdapter() {
  if (handle_) {
    ArecaBambooDestroy(handle_);
  }
}

BambooResult BambooEngineAdapter::process(uint32_t codepoint,
                                          const std::string &utf8Text) {
  BambooResult result;
  result.currentText = renderedText_;

  // Finalize the current word before committing a boundary. With spell check
  // enabled, Bamboo restores an invalid Vietnamese-looking word to the raw
  // Latin keystrokes that produced it.
  if (!ArecaBambooCanProcess(handle_, codepoint)) {
    if (finalizedWordAvailable_) {
      ++trailingBoundaryCount_;
      result.commitText = encode(utf8Text);
      return result;
    }

    char *raw = macroEnabled_
                    ? ArecaBambooExpandMacro(handle_,
                                             capitalizeMacro_ ? 1 : 0)
                    : nullptr;
    if (raw) {
      result.macroExpanded = true;
      ArecaBambooReset(handle_);
    } else {
      raw = ArecaBambooFinalizeWord(handle_, spellCheck_ ? 1 : 0);
    }
    if (!raw) {
      throw std::runtime_error("Bamboo word finalization failed");
    }
    std::string finalized(raw);
    std::free(raw);
    std::string next = finalized;
    next += utf8Text;
    next = encode(next);

    const auto oldChars = codepoints(renderedText_);
    const auto newChars = codepoints(next);
    size_t prefix = 0;
    while (prefix + 1 < oldChars.size() && prefix + 1 < newChars.size() &&
           oldChars[prefix].first == newChars[prefix].first) {
      ++prefix;
    }
    result.deleteCount =
        static_cast<uint32_t>((oldChars.size() - 1) - prefix);
    result.commitText = next.substr(newChars[prefix].second);
    if (!result.macroExpanded && !finalized.empty()) {
      finalizedRenderedText_ = encode(finalized);
      finalizedWordAvailable_ = true;
      trailingBoundaryCount_ = 1;
    } else {
      finalizedRenderedText_.clear();
      finalizedWordAvailable_ = false;
      trailingBoundaryCount_ = 0;
    }
    renderedText_.clear();
    result.newText.clear();
    return result;
  }

  if (finalizedWordAvailable_) {
    ArecaBambooReset(handle_);
    finalizedRenderedText_.clear();
    finalizedWordAvailable_ = false;
    trailingBoundaryCount_ = 0;
  }

  char *raw = ArecaBambooProcess(handle_, codepoint, realtimeSpellcheck_ ? 1 : 0);
  if (!raw) {
    throw std::runtime_error("Bamboo processing failed");
  }
  std::string next = encode(raw);
  std::free(raw);

  const auto oldChars = codepoints(renderedText_);
  const auto newChars = codepoints(next);
  size_t prefix = 0;
  while (prefix + 1 < oldChars.size() && prefix + 1 < newChars.size() &&
         oldChars[prefix].first == newChars[prefix].first) {
    ++prefix;
  }

  result.deleteCount = static_cast<uint32_t>((oldChars.size() - 1) - prefix);
  result.commitText = next.substr(newChars[prefix].second);
  renderedText_ = std::move(next);
  result.newText = renderedText_;
  return result;
}

BambooResult BambooEngineAdapter::processBackspace() {
  BambooResult result;
  result.currentText = renderedText_;
  backspace();
  result.newText = renderedText_;

  const auto oldChars = codepoints(result.currentText);
  const auto newChars = codepoints(result.newText);
  size_t prefix = 0;
  while (prefix + 1 < oldChars.size() && prefix + 1 < newChars.size() &&
         oldChars[prefix].first == newChars[prefix].first) {
    ++prefix;
  }

  result.deleteCount = static_cast<uint32_t>((oldChars.size() - 1) - prefix);
  result.commitText = result.newText.substr(newChars[prefix].second);
  return result;
}

void BambooEngineAdapter::reset() {
  ArecaBambooReset(handle_);
  renderedText_.clear();
  finalizedRenderedText_.clear();
  finalizedWordAvailable_ = false;
  trailingBoundaryCount_ = 0;
}

void BambooEngineAdapter::backspace() {
  if (finalizedWordAvailable_) {
    if (trailingBoundaryCount_ > 1) {
      --trailingBoundaryCount_;
      return;
    }
    trailingBoundaryCount_ = 0;
    finalizedWordAvailable_ = false;
    renderedText_ = std::move(finalizedRenderedText_);
    return;
  }

  char *raw = ArecaBambooBackspace(handle_);
  if (!raw) {
    throw std::runtime_error("Bamboo Backspace processing failed");
  }
  renderedText_ = encode(raw);
  std::free(raw);
}

std::vector<std::string> BambooEngineAdapter::inputMethodNames() {
  return splitLines(ArecaBambooInputMethodNames());
}

std::vector<std::string> BambooEngineAdapter::charsetNames() {
  return splitLines(ArecaBambooCharsetNames());
}

std::string BambooEngineAdapter::encode(const std::string &text) const {
  char *raw = ArecaBambooEncode(const_cast<char *>(outputCharset_.c_str()),
                                const_cast<char *>(text.c_str()));
  if (!raw) {
    throw std::runtime_error("Bamboo output encoding failed");
  }
  std::string encoded(raw);
  std::free(raw);
  return encoded;
}

} // namespace areca
