#include "reliability_checker.h"

#include <algorithm>
#include <array>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

#include "browser_autocomplete.h"
#include "program_compatibility.h"

namespace areca {
namespace {

constexpr uint64_t kForwardBackspaceCapabilityMask = 0x72;

using SurroundingRule = bool (*)(const fcitx::SurroundingText &);

constexpr std::array<SurroundingRule, 4> kInvalidSurroundingRules = {
    [](const auto &s) { return s.cursor() <= 0; },
    [](const auto &s) { return s.anchor() <= 0; },
    [](const auto &s) { return s.text().empty(); },
    [](const auto &s) { return s.text() == "_"; },
};

bool isInvalidSurrounding(const fcitx::SurroundingText &surrounding) {
  return std::any_of(kInvalidSurroundingRules.begin(),
                     kInvalidSurroundingRules.end(),
                     [&](auto rule) { return rule(surrounding); });
}

} // namespace

ReliabilityDecision ReliabilityChecker::evaluate(
    fcitx::InputContext &inputContext, const std::string &shownText,
    SurroundingReliabilityState &state, bool debug) const {
  const auto &surrounding = inputContext.surroundingText();

  const bool browserAutocomplete =
      isBrowserLikeProgram(inputContext.program()) &&
      looksLikeBrowserAutocomplete(surrounding.text(), surrounding.cursor(),
                                   surrounding.anchor(), shownText);

  if (!state.known && !browserAutocomplete) {
    state.reliable = !isInvalidSurrounding(surrounding);
    if (debug) {
      FCITX_INFO() << "areca: reliability first-probe text="
                   << surrounding.text() << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor()
                   << " reliable=" << state.reliable;
    }

    state.forceForwardBackspace = false;
    if (isVSCodeFamilyProgram(inputContext.program())) {
      const uint64_t capabilityMask =
          inputContext.capabilityFlags().toInteger();
      state.forceForwardBackspace =
          capabilityMask == kForwardBackspaceCapabilityMask;
    }
    if (state.forceForwardBackspace && debug) {
      FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                   << " reason=program-compatibility-capability-mask-0x72";
    }
    state.known = true;
  }

  if (debug) {
    FCITX_INFO() << "areca: reliability cached known=" << state.known
                 << " reliable=" << state.reliable
                 << " force_forward=" << state.forceForwardBackspace
                 << " browser_autocomplete=" << browserAutocomplete
                 << " program=" << inputContext.program();
  }

  ReliabilityDecision decision;
  decision.browserAutocomplete = browserAutocomplete;
  decision.useSurrounding = state.known && state.reliable &&
                            !state.forceForwardBackspace &&
                            !browserAutocomplete;
  return decision;
}

} // namespace areca
