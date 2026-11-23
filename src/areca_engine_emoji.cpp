#include "areca_engine.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterfacemanager.h>

#include "emoji_suggestion.h"
#include "fcitx_emoji_compat.h"

namespace areca {

namespace {

// Danh sách dài hơn chỉ làm người dùng phải đọc nhiều.
constexpr std::size_t kMaxEmojiCandidates = 9;

class EmojiCandidateWord final : public fcitx::CandidateWord {
public:
  EmojiCandidateWord(EmojiMatch match,
                     std::function<void(const std::string &)> onSelect)
      : fcitx::CandidateWord(
            fcitx::Text(match.emoji + "  :" + match.name + ":")),
        emoji_(std::move(match.emoji)), onSelect_(std::move(onSelect)) {}

  void select(fcitx::InputContext *) const override {
    if (onSelect_) {
      onSelect_(emoji_);
    }
  }

private:
  std::string emoji_;
  std::function<void(const std::string &)> onSelect_;
};

void appendUnique(std::vector<EmojiMatch> &out, const std::string &name,
                  const std::vector<std::string> &emojis) {
  for (const auto &emoji : emojis) {
    appendEmojiMatch(out, emoji, name, kMaxEmojiCandidates);
  }
}

} // namespace

bool ArecaEngine::isEmojiSuggesting(fcitx::InputContext &inputContext) const {
  auto *candList = inputContext.inputPanel().candidateList().get();
  auto *commonList = dynamic_cast<fcitx::CommonCandidateList *>(candList);
  return commonList && commonList->size() > 0 &&
         dynamic_cast<const EmojiCandidateWord *>(&commonList->candidate(0)) !=
             nullptr;
}

void ArecaEngine::hideEmojiSuggestions(fcitx::InputContext &inputContext) {
  if (!isEmojiSuggesting(inputContext)) {
    return;
  }
  inputContext.inputPanel().reset();
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);
}

void ArecaEngine::refreshEmojiSuggestions(fcitx::InputContext &inputContext) {
  if (!config_.emojiSuggestion.value() || isBackendSelecting(inputContext) ||
      effectivePresentationMode(inputContext) != PresentationMode::Rewrite) {
    hideEmojiSuggestions(inputContext);
    return;
  }
  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  const auto query = state
                         ? emojiQueryBeforeCursor(state->knownTextBeforeCursor)
                         : std::nullopt;
  if (!query) {
    hideEmojiSuggestions(inputContext);
    return;
  }

  // Shortcode kiểu chat lên trước vì người dùng gõ chúng có chủ ý. Sau đó tới
  // từ điển của addon emoji: khớp đúng từ khoá rồi tới các từ khoá bắt đầu
  // bằng nó; tiếng Việt thiếu thì addon lùi về tiếng Anh. Máy không có addon
  // vẫn dùng được shortcode.
  std::vector<EmojiMatch> emojis =
      emojiShortcodeMatches(query->keyword, kMaxEmojiCandidates);
  auto *emoji = emojis.size() < kMaxEmojiCandidates
                    ? instance_->addonManager().addon("emoji", true)
                    : nullptr;
  if (emoji) {
    appendUnique(emojis, query->keyword,
                 emoji->call<fcitx::IEmoji::query>("vi", query->keyword, true));
  }
  if (emoji && emojis.size() < kMaxEmojiCandidates) {
    emoji->call<fcitx::IEmoji::prefix>(
        "vi", query->keyword, true,
        [&emojis](const std::string &name,
                  const std::vector<std::string> &found) {
          appendUnique(emojis, name, found);
          return emojis.size() < kMaxEmojiCandidates;
        });
  }
  if (emojis.empty()) {
    hideEmojiSuggestions(inputContext);
    return;
  }
  if (debugEnabled()) {
    FCITX_INFO() << "areca: emoji suggest keyword=" << query->keyword
                 << " count=" << emojis.size();
  }

  auto candList = std::make_unique<fcitx::CommonCandidateList>();
  candList->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  candList->setPageSize(static_cast<int>(emojis.size()));
  // Chọn bằng mũi tên và Enter như bộ chọn emoji của các ứng dụng chat, nên
  // không đánh số; phím số vẫn gõ chữ số bình thường.
  candList->setLabels(std::vector<std::string>(emojis.size()));
  auto inputContextWatch = inputContext.watch();
  for (auto &value : emojis) {
    candList->append(std::make_unique<EmojiCandidateWord>(
        std::move(value), [this, inputContextWatch](const std::string &chosen) {
          // Chọn bằng chuột tới từ UI: đưa sang pha post để không sửa panel
          // ngay trong lúc UI đang duyệt danh sách.
          emojiSelectionPost_.schedule([this, inputContextWatch, chosen]() {
            if (auto *ic = inputContextWatch.get()) {
              selectEmoji(*ic, chosen);
            }
          });
        }));
  }
  candList->setGlobalCursorIndex(0);

  auto &panel = inputContext.inputPanel();
  panel.reset();
  panel.setCandidateList(std::move(candList));
  inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                   true);
}

void ArecaEngine::selectEmoji(fcitx::InputContext &inputContext,
                              std::string emoji) {
  hideEmojiSuggestions(inputContext);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: emoji select emoji=" << emoji;
  }
  // Đoạn ":từ" được đọc lại lúc tới lượt vì các phím đang xếp hàng có thể vẫn
  // còn đổi nó.
  scheduler_.enqueueReplacement(
      inputContext, std::move(emoji), [this](fcitx::InputContext &ic) {
        auto *state = ic.propertyFor(&rewriteStateFactory_);
        if (!state) {
          return std::string();
        }
        const auto query = emojiQueryBeforeCursor(state->knownTextBeforeCursor);
        return query ? query->replacedText : std::string();
      });
}

bool ArecaEngine::handleEmojiSuggestionKey(fcitx::InputContext &inputContext,
                                           fcitx::KeyEvent &event) {
  const auto key = event.key().normalize();
  const auto sym = key.sym();
  const bool plain = !key.states().testAny(
      fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
                       fcitx::KeyState::Super, fcitx::KeyState::Shift});
  const bool navigation = sym == FcitxKey_Up || sym == FcitxKey_Down ||
                          sym == FcitxKey_Return || sym == FcitxKey_KP_Enter ||
                          sym == FcitxKey_Tab || sym == FcitxKey_Escape;
  if (!plain || !navigation) {
    return false;
  }
  // Danh sách đang giữ các phím này: phím nhả cũng không được lọt vào ứng dụng,
  // nếu không Enter có thể gửi tin nhắn ngay sau khi chọn emoji.
  if (event.isRelease()) {
    return true;
  }
  auto *commonList = dynamic_cast<fcitx::CommonCandidateList *>(
      inputContext.inputPanel().candidateList().get());
  if (!commonList) {
    return false;
  }
  switch (sym) {
  case FcitxKey_Escape:
    hideEmojiSuggestions(inputContext);
    break;
  case FcitxKey_Up:
    commonList->prevCandidate();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    break;
  case FcitxKey_Down:
    commonList->nextCandidate();
    inputContext.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                     true);
    break;
  default: {
    const int index = commonList->cursorIndex();
    if (index >= 0 && index < commonList->size()) {
      commonList->candidate(index).select(&inputContext);
    }
    break;
  }
  }
  return true;
}

} // namespace areca
