#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

namespace areca {

struct MacroDefinition {
  std::string key;
  std::string value;
};

class VietnameseEngine {
public:
  virtual ~VietnameseEngine() = default;
  virtual BambooResult process(uint32_t codepoint,
                               const std::string &utf8Text) = 0;
  virtual BambooResult processBackspace() = 0;
  virtual void backspace() = 0;
  virtual void reset() = 0;
  virtual const std::string &currentText() const = 0;
};

class BambooEngineAdapter final : public VietnameseEngine {
public:
  explicit BambooEngineAdapter(std::string inputMethod = "Telex 2",
                               bool spellCheck = true,
                               bool realtimeSpellcheck = true,
                               bool modernStyle = true,
                               std::string outputCharset = "Unicode",
                               bool macroEnabled = true,
                               bool capitalizeMacro = true,
                               std::vector<MacroDefinition> macros = {});
  ~BambooEngineAdapter() override;

  BambooEngineAdapter(const BambooEngineAdapter &) = delete;
  BambooEngineAdapter &operator=(const BambooEngineAdapter &) = delete;

  BambooResult process(uint32_t codepoint,
                       const std::string &utf8Text) override;
  BambooResult processBackspace() override;
  void backspace() override;
  void reset() override;
  const std::string &currentText() const override { return renderedText_; }
  bool valid() const { return handle_ != 0; }
  static std::vector<std::string> inputMethodNames();
  static std::vector<std::string> charsetNames();

private:
  std::string encode(const std::string &text) const;

  uint64_t handle_ = 0;
  bool spellCheck_ = true;
  bool realtimeSpellcheck_ = true;
  std::string outputCharset_;
  bool macroEnabled_ = true;
  bool capitalizeMacro_ = true;
  std::string renderedText_;
  std::string finalizedRenderedText_;
  uint32_t trailingBoundaryCount_ = 0;
  bool finalizedWordAvailable_ = false;
};

} // namespace areca
