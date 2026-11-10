#pragma once

#include <fcitx-utils/key.h>

namespace areca {

struct SentenceCapitalizationState {
  bool punctuationPending = false;
  bool capitalizeNextLetter = false;

  void reset() {
    punctuationPending = false;
    capitalizeNextLetter = false;
  }
};

// Update sentence-boundary state and return the keysym that should be sent to
// Bamboo. Only ASCII a-z is changed; Bamboo remains responsible for producing
// the selected Vietnamese output charset.
fcitx::KeySym
capitalizeAfterSentenceBoundary(SentenceCapitalizationState &state,
                                fcitx::KeySym sym);

} // namespace areca
