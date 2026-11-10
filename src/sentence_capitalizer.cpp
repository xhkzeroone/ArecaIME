#include "sentence_capitalizer.h"

#include <fcitx-utils/keysym.h>

namespace areca {

fcitx::KeySym
capitalizeAfterSentenceBoundary(SentenceCapitalizationState &state,
                                fcitx::KeySym sym) {
  auto result = sym;
  if (state.capitalizeNextLetter) {
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
      result = static_cast<fcitx::KeySym>(sym - (FcitxKey_a - FcitxKey_A));
      state.capitalizeNextLetter = false;
    } else if (sym != FcitxKey_space) {
      state.capitalizeNextLetter = false;
    }
  }

  switch (result) {
  case FcitxKey_period:
  case FcitxKey_exclam:
  case FcitxKey_question:
    state.punctuationPending = true;
    break;
  case FcitxKey_space:
    if (state.punctuationPending) {
      state.capitalizeNextLetter = true;
      state.punctuationPending = false;
    }
    break;
  default:
    state.punctuationPending = false;
    break;
  }

  return result;
}

} // namespace areca
