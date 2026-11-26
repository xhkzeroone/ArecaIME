#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "emoji_suggestion.h"
#include "input_scheduler.h"

namespace {

void testQueryParsing() {
  auto query = areca::emojiQueryBeforeCursor("hôm nay vui :cười");
  assert(query);
  assert(query->keyword == "cười");
  assert(query->replacedText == ":cười");
  assert(query->replacedLength == 5);

  // Đầu đoạn đã biết cũng được coi là đầu từ.
  query = areca::emojiQueryBeforeCursor(":Tim");
  assert(query && query->keyword == "tim" && query->replacedLength == 4);

  // Từ khoá nhiều chữ.
  query = areca::emojiQueryBeforeCursor(" :mặt cười");
  assert(query && query->keyword == "mặt cười");

  // Giờ, URL, a:b và câu có dấu hai chấm không phải từ khoá.
  assert(!areca::emojiQueryBeforeCursor("10:30"));
  assert(!areca::emojiQueryBeforeCursor("http://abc"));
  assert(!areca::emojiQueryBeforeCursor("a:b"));
  assert(!areca::emojiQueryBeforeCursor("Ghi chú: hôm nay"));
  assert(!areca::emojiQueryBeforeCursor("x :"));
  // Dấu cách cuối nghĩa là đã gõ xong, dấu câu kết thúc từ khoá.
  assert(!areca::emojiQueryBeforeCursor(" :tim "));
  assert(!areca::emojiQueryBeforeCursor(" :tim."));
  assert(!areca::emojiQueryBeforeCursor(" :a  b"));
  assert(!areca::emojiQueryBeforeCursor(" :một hai ba bốn"));
  assert(!areca::emojiQueryBeforeCursor(" :abcdefghijklmnopqrstuvwxyz"));
  assert(!areca::emojiQueryBeforeCursor("không có"));

  // Shortcode kiểu chat có "+-_".
  query = areca::emojiQueryBeforeCursor(" :+1");
  assert(query && query->keyword == "+1" && query->replacedLength == 3);
  query = areca::emojiQueryBeforeCursor(":Thumbs_Up");
  assert(query && query->keyword == "thumbs_up");
  assert(!areca::emojiQueryBeforeCursor(" :-)"));
}

void testShortcodes() {
  // Khớp đúng lên trước, không lặp emoji, có giới hạn.
  auto found = areca::emojiShortcodeMatches("like", 9);
  assert(!found.empty() && found.front().emoji == "👍" &&
         found.front().name == "like");
  found = areca::emojiShortcodeMatches("+1", 9);
  assert(found.size() == 1 && found.front().emoji == "👍" &&
         found.front().name == "+1");
  // "hihi" và "hehe" bắt đầu bằng "h" nhưng khớp tiền tố "ha" chỉ có "haha".
  found = areca::emojiShortcodeMatches("ha", 9);
  assert(!found.empty() && found.front().emoji == "😂" &&
         found.front().name == "haha");
  found = areca::emojiShortcodeMatches("h", 2);
  assert(found.size() == 2);
  assert(areca::emojiShortcodeMatches("khongco", 9).empty());
  // Emoji đã có thì không thêm lần nữa dù tên khác.
  std::vector<areca::EmojiMatch> merged;
  areca::appendEmojiMatch(merged, "👍", "like", 9);
  areca::appendEmojiMatch(merged, "👍", "thumbs up", 9);
  areca::appendEmojiMatch(merged, "😂", "haha", 1);
  assert(merged.size() == 1 && merged.front().name == "like");
  // Shortcode trong bảng không trùng nhau.
  for (std::size_t i = 0; i < areca::kEmojiShortcodes.size(); ++i) {
    for (std::size_t j = i + 1; j < areca::kEmojiShortcodes.size(); ++j) {
      assert(areca::kEmojiShortcodes[i].code !=
             areca::kEmojiShortcodes[j].code);
    }
  }
}

class TestInputContext final : public fcitx::InputContext {
public:
  explicit TestInputContext(fcitx::InputContextManager &manager)
      : fcitx::InputContext(manager, "test-app") {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }
  void commitStringImpl(const std::string &text) override {
    commits.push_back(text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::vector<std::string> commits;
};

class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t) const override { return true; }
  areca::BambooResult process(uint32_t, const std::string &text) override {
    current_ += text;
    areca::BambooResult result;
    result.commitText = text;
    return result;
  }
  areca::BambooResult processBackspace() override { return {}; }
  void backspace() override {}
  void reset() override {
    current_.clear();
    ++resetCount;
  }
  const std::string &currentText() const override { return current_; }

  int resetCount = 0;

private:
  std::string current_;
};

class RecordingBackend final : public areca::RewriteBackend {
public:
  const char *name() const override { return "recording"; }
  areca::MarkerDelivery markerDelivery() const override {
    return areca::MarkerDelivery::Keys;
  }
  areca::ApplyStatus apply(fcitx::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone) override {
    backspaces.push_back(plan.backspaceCount);
    inputContext.commitString(plan.commitText);
    return areca::ApplyStatus::Completed;
  }

  std::vector<uint32_t> backspaces;
};

void testSchedulerReplacement() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  TestEngine engine;
  RecordingBackend backend;
  std::string replacedSeen;
  areca::InputScheduler scheduler(
      loop, [&](fcitx::InputContext &) { return &engine; },
      [](fcitx::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        timing.waylandAfterMarkerCommitWaitMs = 0;
        return timing;
      },
      [] { return false; },
      [&](fcitx::InputContext &, const areca::BambooResult &result) {
        replacedSeen = result.currentText;
        return areca::RewriteBackendSelection{&backend};
      });

  engine.process('c', "c");
  // Đoạn bị thay được đọc lúc tới lượt, tính theo ký tự chứ không theo byte.
  scheduler.enqueueReplacement(context, "😀", [](fcitx::InputContext &) {
    return std::string(":cười");
  });
  // Thêm một ký tự cho marker commit trước lượt xoá.
  assert((backend.backspaces == std::vector<uint32_t>{6}));
  assert((context.commits == std::vector<std::string>{"\u00b7", "😀"}));
  assert(replacedSeen == ":cười");
  assert(engine.resetCount == 1);
  assert(engine.currentText().empty());

  // Không còn ":từ" khi tới lượt thì không xoá hay chèn gì.
  scheduler.enqueueReplacement(
      context, "😀", [](fcitx::InputContext &) { return std::string(); });
  assert(backend.backspaces.size() == 1);
  assert(context.commits.size() == 2);
}

} // namespace

int main() {
  testQueryParsing();
  testShortcodes();
  testSchedulerReplacement();
  std::cout << "Emoji suggestion test passed successfully\n";
  return 0;
}
