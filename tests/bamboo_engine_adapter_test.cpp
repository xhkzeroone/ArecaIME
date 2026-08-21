#include <cassert>
#include <algorithm>
#include <iostream>
#include <string>

#include <fcitx-utils/utf8.h>

#include "bamboo_engine_adapter.h"

namespace {

areca::BambooResult type(areca::BambooEngineAdapter &engine, char key) {
  return engine.process(static_cast<unsigned char>(key), std::string(1, key));
}

void applyToDisplay(std::string &display,
                    const areca::BambooResult &result) {
  const auto displayLength = fcitx::utf8::length(display);
  assert(displayLength >= result.deleteCount);
  const auto eraseFrom = fcitx::utf8::nextNChar(
      display.begin(), displayLength - result.deleteCount);
  display.erase(eraseFrom, display.end());
  display += result.commitText;
}

} // namespace

int main() {
  areca::BambooEngineAdapter engine("Telex 2");

  auto a = type(engine, 'a');
  assert(a.currentText.empty());
  assert(a.newText == "a");
  assert(a.deleteCount == 0);
  assert(a.commitText == "a");

  auto w = type(engine, 'w');
  assert(w.currentText == "a");
  assert(w.newText == "ă");
  assert(w.deleteCount == 1);
  assert(w.commitText == "ă");

  engine.backspace();
  engine.reset();
  a = type(engine, 'a');
  w = type(engine, 'w');

  auto n = type(engine, 'n');
  assert(n.currentText == "ă");
  assert(n.newText == "ăn");
  assert(n.deleteCount == 0);
  assert(n.commitText == "n");

  auto space = type(engine, ' ');
  assert(space.currentText == "ăn");
  assert(space.newText.empty());
  assert(space.deleteCount == 0);
  assert(space.commitText == " ");

  engine.reset();
  std::string display;
  for (char key : std::string("chuaarn")) {
    const auto result = type(engine, key);
    const auto displayLength = fcitx::utf8::length(display);
    assert(displayLength >= result.deleteCount);
    if (result.deleteCount) {
      const auto eraseFrom = fcitx::utf8::nextNChar(
          display.begin(), displayLength - result.deleteCount);
      display.erase(eraseFrom, display.end());
    }
    display += result.commitText;
  }
  assert(display == "chuẩn");

  // Crossing a word boundary with Backspace restores the finalized Bamboo
  // composition, so the previous word can still be edited.
  engine.reset();
  display.clear();
  for (char key : std::string("as ")) {
    applyToDisplay(display, type(engine, key));
  }
  assert(display == "á ");
  display.pop_back();
  engine.backspace();
  const auto retonePreviousWord = type(engine, 'f');
  assert(retonePreviousWord.currentText == "á");
  applyToDisplay(display, retonePreviousWord);
  assert(display == "à");

  // Multiple trailing boundaries must all be crossed before restoring the
  // preceding word.
  engine.reset();
  display.clear();
  for (char key : std::string("as  ")) {
    applyToDisplay(display, type(engine, key));
  }
  assert(display == "á  ");
  display.pop_back();
  engine.backspace();
  display.pop_back();
  engine.backspace();
  const auto retoneAfterTwoSpaces = type(engine, 'f');
  assert(retoneAfterTwoSpaces.currentText == "á");
  applyToDisplay(display, retoneAfterTwoSpaces);
  assert(display == "à");

  engine.reset();
  display.clear();
  for (char key : std::string("as ")) {
    applyToDisplay(display, type(engine, key));
  }
  display.pop_back();
  engine.backspace();
  display.clear();
  engine.backspace();
  const auto afterDeletingPreviousWord = type(engine, 'b');
  assert(afterDeletingPreviousWord.currentText.empty());
  applyToDisplay(display, afterDeletingPreviousWord);
  assert(display == "b");

  engine.reset();
  display.clear();
  for (char key : std::string("as b")) {
    applyToDisplay(display, type(engine, key));
  }
  const auto newWordTone = type(engine, 's');
  assert(newWordTone.currentText == "b");

  // With real-time spell check, an invalid Vietnamese-looking syllable
  // is restored to the original Latin keystrokes immediately.
  engine.reset();
  display.clear();
  for (char key : std::string("awbc")) {
    const auto result = type(engine, key);
    const auto displayLength = fcitx::utf8::length(display);
    const auto eraseFrom = fcitx::utf8::nextNChar(
        display.begin(), displayLength - result.deleteCount);
    display.erase(eraseFrom, display.end());
    display += result.commitText;
  }
  assert(display == "awbc");
  auto checkedBoundary = type(engine, ' ');
  assert(checkedBoundary.currentText == "awbc");
  assert(checkedBoundary.deleteCount == 0);
  assert(checkedBoundary.commitText == " ");

  areca::BambooEngineAdapter uncheckedEngine("Telex 2", false, false);
  for (char key : std::string("awbc")) {
    type(uncheckedEngine, key);
  }
  auto uncheckedBoundary = type(uncheckedEngine, ' ');
  assert(uncheckedBoundary.currentText == "ăbc");
  assert(uncheckedBoundary.deleteCount == 0);
  assert(uncheckedBoundary.commitText == " ");

  areca::BambooEngineAdapter basicSpellcheckEngine("Telex 2", true, false);
  for (char key : std::string("awbc")) {
    type(basicSpellcheckEngine, key);
  }
  auto basicBoundary = type(basicSpellcheckEngine, ' ');
  assert(basicBoundary.currentText == "ăbc");
  assert(basicBoundary.deleteCount == 3);
  assert(basicBoundary.commitText == "awbc ");

  areca::BambooEngineAdapter modernToneEngine("Telex 2", true, true, true);
  areca::BambooEngineAdapter traditionalToneEngine("Telex 2", true, true, false);
  areca::BambooResult modernTone;
  areca::BambooResult traditionalTone;
  for (char key : std::string("hoaf")) {
    modernTone = type(modernToneEngine, key);
    traditionalTone = type(traditionalToneEngine, key);
  }
  assert(modernTone.newText == "hoà");
  assert(traditionalTone.newText == "hòa");

  const auto inputMethods = areca::BambooEngineAdapter::inputMethodNames();
  assert(std::find(inputMethods.begin(), inputMethods.end(), "VNI") !=
         inputMethods.end());
  assert(std::find(inputMethods.begin(), inputMethods.end(), "VIQR") !=
         inputMethods.end());
  for (const auto &inputMethod : inputMethods) {
    areca::BambooEngineAdapter availableMethod(inputMethod);
    assert(availableMethod.valid());
  }
  const auto charsets = areca::BambooEngineAdapter::charsetNames();
  assert(!charsets.empty() && charsets.front() == "Unicode");
  assert(std::find(charsets.begin(), charsets.end(), "Unicode tổ hợp") !=
         charsets.end());
  for (const auto &charset : charsets) {
    areca::BambooEngineAdapter encodedEngine("Telex 2", true, true, true, charset);
    type(encodedEngine, 'a');
    const auto encodedResult = type(encodedEngine, 's');
    assert(fcitx::utf8::validate(encodedResult.newText));
  }

  areca::BambooEngineAdapter vniEngine("VNI");
  areca::BambooResult vniResult;
  for (char key : std::string("a61")) {
    vniResult = type(vniEngine, key);
  }
  assert(vniResult.newText == "ấ");

  areca::BambooEngineAdapter combiningEngine(
      "Telex 2", true, true, true, "Unicode tổ hợp");
  type(combiningEngine, 'a');
  const auto combiningResult = type(combiningEngine, 's');
  assert(combiningResult.newText == "a\u0301");
  assert(combiningResult.deleteCount == 0);
  assert(combiningResult.commitText == "\u0301");

  const std::vector<areca::MacroDefinition> macros = {
      {"bt", "Be There"}, {"vn", "Việt Nam"}};
  areca::BambooEngineAdapter macroEngine("Telex 2", true, true, true, "Unicode",
                                         true, true, macros);
  display.clear();
  for (char key : std::string("bt ")) {
    const auto result = type(macroEngine, key);
    applyToDisplay(display, result);
    if (key == ' ') {
      assert(result.macroExpanded);
    }
  }
  assert(display == "be there ");

  areca::BambooEngineAdapter uppercaseMacroEngine(
      "Telex 2", true, true, true, "Unicode", true, true, macros);
  display.clear();
  for (char key : std::string("BT ")) {
    applyToDisplay(display, type(uppercaseMacroEngine, key));
  }
  assert(display == "BE THERE ");

  areca::BambooEngineAdapter disabledMacroEngine(
      "Telex 2", true, true, true, "Unicode", false, true, macros);
  display.clear();
  for (char key : std::string("bt ")) {
    applyToDisplay(display, type(disabledMacroEngine, key));
  }
  assert(display == "bt ");

  areca::BambooEngineAdapter encodedMacroEngine(
      "Telex 2", true, true, true, "Unicode tổ hợp", true, false, macros);
  display.clear();
  for (char key : std::string("vn ")) {
    applyToDisplay(display, type(encodedMacroEngine, key));
  }
  assert(display == "Viê\u0323t Nam ");

  // Test spellcheck mistake recovery on Backspace:
  // Typing "nhanhsh" (typo of "nhánh" + extra "h"), calling processBackspace()
  // restores the valid Vietnamese syllable "nhánh".
  engine.reset();
  display.clear();
  for (char key : std::string("nhanhsh")) {
    applyToDisplay(display, type(engine, key));
  }
  assert(display == "nhanhsh");
  auto backspaceResult = engine.processBackspace();
  applyToDisplay(display, backspaceResult);
  assert(display == "nhánh");

  std::cout << "Bamboo adapter tests passed\n";
  return 0;
}
