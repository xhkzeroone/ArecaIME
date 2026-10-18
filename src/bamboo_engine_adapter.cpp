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
char *ArecaBambooProcess(uint64_t id, uint32_t key, int spellCheck,
                         int *spellcheckRestored);
char *ArecaBambooFinalizeWord(uint64_t id, int spellCheck,
                              int *spellcheckRestored);
char *ArecaBambooBackspace(uint64_t id);
char *ArecaBambooRecoverBackspace(uint64_t id);
int ArecaBambooRestore(uint64_t id, char *visibleText);
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
                                         std::vector<MacroDefinition> macros,
                                         bool revertMacroOnBackspace)
    : spellCheck_(spellCheck), realtimeSpellcheck_(realtimeSpellcheck),
      outputCharset_(std::move(outputCharset)),
      macroEnabled_(macroEnabled), capitalizeMacro_(capitalizeMacro),
      revertMacroOnBackspace_(revertMacroOnBackspace) {
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

bool BambooEngineAdapter::canProcessKey(uint32_t codepoint) const {
  return ArecaBambooCanProcess(handle_, codepoint) != 0;
}

BambooResult BambooEngineAdapter::process(uint32_t codepoint,
                                          const std::string &utf8Text) {
  BambooResult result;
  result.currentText = renderedText_;
  spellcheckRestored_ = false;

  // Finalize the current word before committing a boundary. With spell check
  // enabled, Bamboo restores an invalid Vietnamese-looking word to the raw
  // Latin keystrokes that produced it.
  if (!canProcessKey(codepoint)) {
    if (finalizedWordAvailable_) {
      macroRecoveryAvailable_ = false;
      macroExpandedTextWithBoundary_.clear();
      macroRawKeyWithBoundary_.clear();
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
      int spellcheckRestored = 0;
      raw = ArecaBambooFinalizeWord(handle_, spellCheck_ ? 1 : 0,
                                    &spellcheckRestored);
      result.spellcheckRestored = spellcheckRestored != 0;
      spellcheckRestored_ = result.spellcheckRestored;
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
      macroRecoveryAvailable_ = false;
      macroExpandedTextWithBoundary_.clear();
      macroRawKeyWithBoundary_.clear();
    } else if (result.macroExpanded && revertMacroOnBackspace_) {
      finalizedRenderedText_.clear();
      finalizedWordAvailable_ = false;
      trailingBoundaryCount_ = 0;
      macroRecoveryAvailable_ = true;
      macroExpandedTextWithBoundary_ = next;
      std::string rawWithBoundary = renderedText_;
      rawWithBoundary += utf8Text;
      macroRawKeyWithBoundary_ = encode(rawWithBoundary);
    } else {
      finalizedRenderedText_.clear();
      finalizedWordAvailable_ = false;
      trailingBoundaryCount_ = 0;
      macroRecoveryAvailable_ = false;
      macroExpandedTextWithBoundary_.clear();
      macroRawKeyWithBoundary_.clear();
    }
    renderedText_.clear();
    result.newText.clear();
    return result;
  }

  macroRecoveryAvailable_ = false;
  macroExpandedTextWithBoundary_.clear();
  macroRawKeyWithBoundary_.clear();
  if (finalizedWordAvailable_) {
    ArecaBambooReset(handle_);
    finalizedRenderedText_.clear();
    finalizedWordAvailable_ = false;
    trailingBoundaryCount_ = 0;
  }

  int spellcheckRestored = 0;
  char *raw = ArecaBambooProcess(handle_, codepoint,
                                 realtimeSpellcheck_ ? 1 : 0,
                                 &spellcheckRestored);
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
  result.spellcheckRestored = spellcheckRestored != 0;
  spellcheckRestored_ = result.spellcheckRestored;
  return result;
}

BambooResult BambooEngineAdapter::processBackspace() {
  BambooResult result;
  result.currentText = renderedText_;

  if (macroRecoveryAvailable_) {
    macroRecoveryAvailable_ = false;
    result.currentText = macroExpandedTextWithBoundary_;
    result.newText = macroRawKeyWithBoundary_;

    const auto oldChars = codepoints(result.currentText);
    const auto newChars = codepoints(result.newText);
    size_t prefix = 0;
    while (prefix + 1 < oldChars.size() && prefix + 1 < newChars.size() &&
           oldChars[prefix].first == newChars[prefix].first) {
      ++prefix;
    }

    result.deleteCount = static_cast<uint32_t>((oldChars.size() - 1) - prefix);
    result.commitText = result.newText.substr(newChars[prefix].second);

    macroExpandedTextWithBoundary_.clear();
    macroRawKeyWithBoundary_.clear();
    renderedText_.clear();
    spellcheckRestored_ = false;
    return result;
  }

  char *raw = ArecaBambooRecoverBackspace(handle_);
  if (!raw) {
    throw std::runtime_error("Bamboo Backspace processing failed");
  }
  renderedText_ = encode(raw);
  std::free(raw);
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
  spellcheckRestored_ = false;
  return result;
}

void BambooEngineAdapter::reset() {
  ArecaBambooReset(handle_);
  renderedText_.clear();
  finalizedRenderedText_.clear();
  finalizedWordAvailable_ = false;
  trailingBoundaryCount_ = 0;
  macroRecoveryAvailable_ = false;
  spellcheckRestored_ = false;
  macroExpandedTextWithBoundary_.clear();
  macroRawKeyWithBoundary_.clear();
}

bool BambooEngineAdapter::restoreFromRenderedText(const std::string &text) {
  if (text.empty() ||
      !ArecaBambooRestore(handle_, const_cast<char *>(text.c_str()))) {
    return false;
  }

  // Bridge đã dựng lại composition Bamboo sao cho đầu ra khớp text đang hiện.
  // Đồng bộ renderedText_ để lần process kế tiếp tính deleteCount trực tiếp với
  // từ cũ và tái sử dụng toàn bộ backend rewrite hiện có.
  renderedText_ = text;
  finalizedRenderedText_.clear();
  finalizedWordAvailable_ = false;
  trailingBoundaryCount_ = 0;
  macroRecoveryAvailable_ = false;
  spellcheckRestored_ = false;
  macroExpandedTextWithBoundary_.clear();
  macroRawKeyWithBoundary_.clear();
  return true;
}

void BambooEngineAdapter::backspace() {
  spellcheckRestored_ = false;
  macroRecoveryAvailable_ = false;
  macroExpandedTextWithBoundary_.clear();
  macroRawKeyWithBoundary_.clear();
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
